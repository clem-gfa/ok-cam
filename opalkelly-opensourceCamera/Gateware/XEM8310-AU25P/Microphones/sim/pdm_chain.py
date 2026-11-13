#!/usr/bin/env python3
"""Modele bit-exact de la chaine de decimation PDM -> PCM prevue pour les 64 micros.

PDM 4,8 MHz (mode ultrason du SPH0641LU4H-1)
  -> CIC ordre 5, R=12 (entier, 19 bits)  -> 400 kS/s
  -> FIR passe-bas + compensation du droop du CIC, decimation 2 -> 200 kS/s, 24 bits

Le modulateur sigma-delta est un modele generique d'ordre 2 : il sert a valider les filtres
(reponse, repliement, longueurs de mots), PAS a predire le bruit reel du micro.

Usage : python3 pdm_chain.py        (numpy uniquement)
"""
import numpy as np

FPDM = 4.8e6
CIC_N, CIC_R = 5, 12
FIR_TAPS, FIR_DEC = 63, 2
FS1 = FPDM / CIC_R            # 400 kS/s
FS = FS1 / FIR_DEC            # 200 kS/s
BAND = 80e3                   # bande utile specifiee par la datasheet


def sigma_delta2(x):
    """Modulateur 1 bit d'ordre 2 (x dans [-0.7, 0.7]); sortie +/-1."""
    y = np.empty_like(x)
    i1 = i2 = 0.0
    q = 1.0
    for n, v in enumerate(x):
        i1 += v - q
        i2 += i1 - q
        q = 1.0 if i2 >= 0 else -1.0
        y[n] = q
    return y


def cic_decimate(bits):
    """CIC entier (Hogenauer) : integrateurs 64 bits, pas de troncature intermediaire."""
    x = bits.astype(np.int64)
    for _ in range(CIC_N):
        x = np.cumsum(x)
    x = x[CIC_R - 1::CIC_R]
    for _ in range(CIC_N):
        x = np.diff(x, prepend=0)
    return x                  # gain DC = R^N


def fir_design():
    """FIR a phase lineaire : passe-bas (coupure ~0,42*FS1/2) x inverse de sinc^N, fenetre de Kaiser."""
    n = np.arange(FIR_TAPS) - (FIR_TAPS - 1) / 2
    fc = 100e3 / FS1                    # milieu de la transition 80 -> 120 kHz (fraction de FS1)
    h = 2 * fc * np.sinc(2 * fc * n) * np.kaiser(FIR_TAPS, 8.0)
    # compensation : echantillonnage frequentiel de 1/|sinc^N| jusqu'a BAND
    f = np.linspace(0, 0.5, 512)
    fr = f * FS1
    d = np.where(fr == 0, 1.0, (np.sin(np.pi * fr * CIC_R / FPDM) / (CIC_R * np.sin(np.pi * fr / FPDM) + 1e-30)) ** CIC_N)
    frc = np.minimum(fr, BAND)          # au-dela de la bande : on garde la correction du bord (pas de coupure)
    d = (np.sin(np.pi * frc * CIC_R / FPDM) / (CIC_R * np.sin(np.pi * frc / FPDM) + 1e-30)) ** CIC_N
    comp = 1.0 / np.maximum(np.abs(d), 1e-3)
    comp = np.clip(comp, 0, 4)
    # filtre de compensation court (11 coefficients) par TF inverse
    k = np.arange(-5, 6)
    hc = np.array([np.sum(comp * np.cos(2 * np.pi * f * kk)) for kk in k]) / len(f)
    hc *= np.kaiser(len(k), 4.0)
    hc /= hc.sum()
    h = np.convolve(h, hc)
    return h / h.sum()


def chain(bits):
    c = cic_decimate(bits) / float(CIC_R ** CIC_N)
    h = fir_design()
    y = np.convolve(c, h, mode="same")[::FIR_DEC]
    return y


def tone_db(y, f0, fs, skip=400):
    y = y[skip:]
    t = np.arange(len(y)) / fs
    c = np.exp(-2j * np.pi * f0 * t)
    return 20 * np.log10(np.abs(np.mean(y * c * np.hanning(len(y))) * 2 / np.mean(np.hanning(len(y)))) + 1e-12)


def main():
    nout = 4096
    npdm = nout * CIC_R * FIR_DEC + 2000
    t = np.arange(npdm) / FPDM
    amp = 0.5  # -6 dBFS
    print(f"CIC N={CIC_N} R={CIC_R}: largeur de mot = {int(np.ceil(CIC_N*np.log2(CIC_R)))+1} bits ; FIR {FIR_TAPS}+10 coefficients")
    print(f"Sortie {FS/1e3:.0f} kS/s, bande utile {BAND/1e3:.0f} kHz\n")
    print(" f_in (kHz) | niveau mesure (dB re entree) | commentaire")
    for f0, note in [(1e3, "reference"), (10e3, ""), (25e3, ""), (40e3, ""), (60e3, ""), (79e3, "bord de bande")]:
        y = chain(sigma_delta2(amp * np.sin(2 * np.pi * f0 * t)))
        g = tone_db(y, f0, FS) - 20 * np.log10(amp)
        print(f" {f0/1e3:9.1f} | {g:+8.2f}")
    print("\nRepliement (tons au-dessus de Nyquist = 100 kHz, mesures a la frequence repliee) :")
    for f0 in (125e3, 140e3, 160e3, 190e3):
        fa = abs(((f0 + FS / 2) % FS) - FS / 2)
        y = chain(sigma_delta2(amp * np.sin(2 * np.pi * f0 * t)))
        g = tone_db(y, fa, FS) - 20 * np.log10(amp)
        print(f" {f0/1e3:6.1f} kHz -> replie a {fa/1e3:5.1f} kHz : {g:+7.1f} dB")
    # Bruit : sinus -6 dBFS a 10 kHz, SNR dans 20 Hz-80 kHz
    y = chain(sigma_delta2(amp * np.sin(2 * np.pi * 10e3 * t)))[400:]
    n = len(y) & ~1
    w = np.hanning(n)
    s = np.abs(np.fft.rfft(y[:n] * w)) ** 2
    fr = np.fft.rfftfreq(n, 1 / FS)
    sig = s[(fr > 9e3) & (fr < 11e3)].sum()
    noise = s[(fr > 100) & (fr < BAND) & ~((fr > 9e3) & (fr < 11e3))].sum()
    print(f"\nSNR modele (2e ordre, 100 Hz-80 kHz) : {10*np.log10(sig/noise):.1f} dB (ne reflete pas le micro reel)")


# ---------------------------------------------------------------------------------------------
# Version entiere (celle du RTL) : coefficients Q17 symetriques, sortie = accumulateur >> 12.
# ---------------------------------------------------------------------------------------------
COEF_FRAC = 17
OUT_SHIFT = 12            # CIC gain R^N * 2^17 / 2^12 -> ~0,95 * 2^23 pour un PDM a pleine echelle


def fir_q17():
    """37 coefficients uniques (k = 0..36) du filtre symetrique de 73 coefficients, en Q17."""
    h = fir_design()
    assert len(h) == 2 * 36 + 1
    half = (h[:37] + h[::-1][:37]) / 2
    return np.round(half * (1 << COEF_FRAC)).astype(np.int64)


def fir_int(c, coef=None):
    """c : sorties entieres du CIC (400 kS/s). Retourne y_full[n] >> 12 pour n pair (200 kS/s)."""
    coef = fir_q17() if coef is None else coef
    c = np.asarray(c, dtype=np.int64)
    x = lambda i: c[i] if 0 <= i < len(c) else 0
    out = []
    for n in range(0, len(c), 2):
        acc = int(coef[36]) * int(x(n - 36))
        for k in range(36):
            acc += int(coef[k]) * (int(x(n - k)) + int(x(n - 72 + k)))
        out.append(acc >> OUT_SHIFT)
    return np.array(out, dtype=np.int64)


if __name__ == "__main__":
    main()
