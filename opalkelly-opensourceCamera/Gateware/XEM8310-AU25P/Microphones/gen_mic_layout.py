#!/usr/bin/env python3
"""Plans des 64 micros du PCB DARWIN a partir de mic_map.csv (colonnes canal_app, x_mm, y_mm : coordonnees KiCad du centre de
chaque SPH0641LU4H-1, tous sur la face avant F.Cu).

  mic_layout_acoustique.svg : vue COTE PORT ACOUSTIQUE, tournee de 90 degres vers la droite (horizontale de la camera). Les micros sont "bottom port" (le son entre par un trou du PCB, cote face
                              arriere) : on regarde la carte par l'arriere, donc l'image est le MIROIR gauche-droite de la vue des
                              composants. C'est la vue pour raisonner sur l'ecoute (sources, antenne...).
  mic_layout_acoustique_miroir.svg : meme vue sans la rotation de 90 degres.
  mic_layout_composants.svg : vue du dessus (cote composants, comme dans KiCad).

Usage : python3 gen_mic_layout.py
"""
import csv, os

HERE = os.path.dirname(os.path.abspath(__file__))
X0, Y0, X1, Y1 = 50.0, 50.0, 220.0, 150.0      # contour de la carte (Edge.Cuts), mm
S = 7.0                                          # pixels par mm
HOLE = (100.0, 100.0, 3.0)                       # trou central (x, y, rayon)
MIC_W, MIC_H = 3.5, 2.65                         # boitier SPH0641LU4H-1, mm
BOARD_W, BOARD_H = (X1 - X0) * S, (Y1 - Y0) * S


def load():
    mics = {}
    with open(os.path.join(HERE, "mic_map.csv"), newline="") as f:
        for r in csv.DictReader(f):
            mics[int(r["canal_app"])] = (float(r["x_mm"]), float(r["y_mm"]))
    assert len(mics) == 64
    return mics


def render(mics, mirrored, rot90=False):
    """mirrored: vue par la face arriere (miroir gauche-droite) ; rot90: puis rotation de 90 degres vers la droite (sens horaire)."""
    # coordonnees d'ecran en mm depuis le coin haut-gauche de la vue : u vers la droite, v vers le bas
    def uv(x, y):
        u = (X1 - x) if mirrored else (x - X0)
        v = y - Y0
        if rot90:                      # rotation horaire de 90 degres : (u, v) -> (hauteur - v, u)
            u, v = (Y1 - Y0) - v, u
        return u, v

    def px(x, y):
        u, v = uv(x, y)
        return u * S, v * S

    vw, vh = ((Y1 - Y0), (X1 - X0)) if rot90 else ((X1 - X0), (Y1 - Y0))
    W, H = vw * S, vh * S
    if mirrored and rot90:
        title = "DARWIN Rev1 — micros côté port acoustique, horizontale caméra"
        sub = ("Face arrière (miroir de la vue KiCad) tournée de 90° vers la droite. Numéro = canal de l'appli = MK. "
               "Graduations : x KiCad à gauche, y en bas.")
    elif mirrored:
        title = "DARWIN Rev1 — micros côté port acoustique (face arrière), canal de l'appli = numéro MK"
        sub = "Image en MIROIR gauche-droite de la vue KiCad (micros « bottom port »). Graduations : x KiCad de 220 à gauche à 50 à droite."
    else:
        title = "DARWIN Rev1 — micros côté composants (dessus, comme dans KiCad), canal de l'appli = numéro MK"
        sub = "Origine KiCad (50, 50) mm en haut à gauche, x vers la droite, y vers le bas."
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="-40 -60 {W + 80} {H + 120}" width="{W + 80}" height="{H + 120}" '
         f'font-family="Helvetica,Arial,sans-serif">',
         f'<rect x="-40" y="-60" width="{W + 80}" height="{H + 120}" fill="#ffffff"/>',
         f'<text x="-30" y="-40" font-size="15" font-weight="bold" fill="#222">{title}</text>',
         f'<text x="-30" y="-25" font-size="11" fill="#555">{sub}</text>',
         f'<text x="-30" y="-11" font-size="11" fill="#555">Plein vert = micro « haut » (MK impair, SELECT = VDD) ; '
         f'creux orange = micro « bas » (MK pair, SELECT = GND).</text>',
         f'<rect x="0" y="0" width="{W}" height="{H}" rx="{2 * S}" fill="#f4f6f4" stroke="#222" stroke-width="2"/>']
    # grille tous les 10 mm (coordonnees KiCad), etiquetee sur les bords
    for gx in range(int(X0), int(X1) + 1, 10):
        (xa, ya), (xb, yb) = px(gx, Y0), px(gx, Y1)
        o.append(f'<line x1="{xa}" y1="{ya}" x2="{xb}" y2="{yb}" stroke="#dde3dd"/>')
        if rot90:   # lignes de x constant : horizontales, etiquette a gauche
            o.append(f'<text x="-6" y="{ya + 4}" font-size="11" fill="#777" text-anchor="end">x {gx}</text>')
        else:
            o.append(f'<text x="{max(xa, xb)}" y="{H + 16}" font-size="11" fill="#777" text-anchor="middle">{gx}</text>' if False else
                     f'<text x="{xa}" y="{H + 16}" font-size="11" fill="#777" text-anchor="middle">{gx}</text>')
    for gy in range(int(Y0), int(Y1) + 1, 10):
        (xa, ya), (xb, yb) = px(X0, gy), px(X1, gy)
        o.append(f'<line x1="{xa}" y1="{ya}" x2="{xb}" y2="{yb}" stroke="#dde3dd"/>')
        if rot90:   # lignes de y constant : verticales, etiquette en bas
            o.append(f'<text x="{xa}" y="{H + 16}" font-size="11" fill="#777" text-anchor="middle">y {gy}</text>')
        else:
            o.append(f'<text x="-6" y="{ya + 4}" font-size="11" fill="#777" text-anchor="end">{gy}</text>')
    cx, cy = px(HOLE[0], HOLE[1])
    o.append(f'<circle cx="{cx}" cy="{cy}" r="{HOLE[2] * S}" fill="#fff" stroke="#222" stroke-width="2"/>'
             f'<text x="{cx}" y="{cy + 4}" font-size="10" fill="#555" text-anchor="middle">trou Ø6</text>')
    # connecteurs (face arriere du PCB) et JTAG (face avant) : reperes seulement
    for lab, x, y, back in (("MC2", 156, 92, True), ("MC3", 185, 67.5, True), ("MC1", 214, 92, True), ("J2 JTAG", 141.2, 58.5, False)):
        a, b = px(x, y)
        suffix = (" (module XEM, face arrière)" if back and not mirrored else "") + (" (face avant, caché)" if (not back and mirrored) else "")
        o.append(f'<text x="{a}" y="{b}" font-size="12" fill="#888" text-anchor="middle">{lab}{suffix}</text>')
    w, h = (MIC_H, MIC_W) if rot90 else (MIC_W, MIC_H)
    w, h = w * S, h * S
    for n in range(1, 65):
        x, y = px(*mics[n])
        top = n % 2 == 1
        fill, stroke, txt = ("#3fa968", "#1f6b40", "#fff") if top else ("#ffffff", "#d9772b", "#a4501a")
        dash = ' stroke-dasharray="4 3"' if mirrored else ""      # vu par l'arriere : le boitier est de l'autre cote du PCB
        o.append(f'<rect x="{x - w / 2}" y="{y - h / 2}" width="{w}" height="{h}" rx="2" fill="{fill}" stroke="{stroke}" stroke-width="2"{dash}/>')
        o.append(f'<text x="{x}" y="{y + 4}" font-size="11" font-weight="bold" fill="{txt}" text-anchor="middle">{n}</text>')
    o.append("</svg>")
    return "\n".join(o)


if __name__ == "__main__":
    m = load()
    open(os.path.join(HERE, "mic_layout_acoustique.svg"), "w").write(render(m, True, rot90=True))        # vue principale
    open(os.path.join(HERE, "mic_layout_acoustique_miroir.svg"), "w").write(render(m, True))             # miroir seul, sans rotation
    open(os.path.join(HERE, "mic_layout_composants.svg"), "w").write(render(m, False))
    print("mic_layout_acoustique.svg (miroir + 90 deg a droite), mic_layout_acoustique_miroir.svg, mic_layout_composants.svg")
