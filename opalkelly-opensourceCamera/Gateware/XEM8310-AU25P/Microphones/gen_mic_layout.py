#!/usr/bin/env python3
"""Plans des 64 micros du PCB DARWIN a partir de mic_map.csv (colonnes canal_app, x_mm, y_mm : coordonnees KiCad du centre de
chaque SPH0641LU4H-1, tous sur la face avant F.Cu).

  mic_layout_acoustique.svg : vue COTE PORT ACOUSTIQUE. Les micros sont "bottom port" (le son entre par un trou du PCB, cote face
                              arriere) : on regarde la carte par l'arriere, donc l'image est le MIROIR gauche-droite de la vue des
                              composants. C'est la vue pour raisonner sur l'ecoute (sources, antenne...).
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


def render(mics, mirrored):
    # vue arriere : x' = X0 + X1 - x (la carte est retournee autour de l'axe vertical)
    def px(x, y):
        xx = (X0 + X1 - x) if mirrored else x
        return (xx - X0) * S, (y - Y0) * S

    title = ("DARWIN Rev1 — micros vus côté port acoustique (face arrière), canal de l'appli = numéro MK" if mirrored
             else "DARWIN Rev1 — micros vus côté composants (dessus, comme dans KiCad), canal de l'appli = numéro MK")
    sub = ("Image en MIROIR gauche-droite de la vue KiCad (micros « bottom port » : le son entre par la face arrière). "
           "Les graduations donnent la coordonnée x KiCad (de 220 à gauche à 50 à droite)." if mirrored else
           "Origine KiCad (50, 50) mm en haut à gauche, x vers la droite, y vers le bas.")
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="-30 -50 {BOARD_W + 60} {BOARD_H + 110}" width="{BOARD_W + 60}" '
         f'height="{BOARD_H + 110}" font-family="Helvetica,Arial,sans-serif">',
         f'<rect x="-30" y="-50" width="{BOARD_W + 60}" height="{BOARD_H + 110}" fill="#ffffff"/>',
         f'<text x="0" y="-33" font-size="17" font-weight="bold" fill="#222">{title}</text>',
         f'<text x="0" y="-18" font-size="11.5" fill="#555">{sub}</text>',
         f'<text x="0" y="-4" font-size="11.5" fill="#555">Carte 170 × 100 mm. Plein vert = micro « haut » (MK impair, SELECT = VDD) ; '
         f'creux orange = micro « bas » (MK pair, SELECT = GND).</text>',
         f'<rect x="0" y="0" width="{BOARD_W}" height="{BOARD_H}" rx="{2 * S}" fill="#f4f6f4" stroke="#222" stroke-width="2"/>']
    for gx in range(int(X0), int(X1) + 1, 10):
        x, _ = px(gx, 0)
        o.append(f'<line x1="{x}" y1="0" x2="{x}" y2="{BOARD_H}" stroke="#dde3dd"/>')
        o.append(f'<text x="{x}" y="{BOARD_H + 16}" font-size="11" fill="#777" text-anchor="middle">{gx}</text>')
    for gy in range(int(Y0), int(Y1) + 1, 10):
        _, y = px(0, gy)
        o.append(f'<line x1="0" y1="{y}" x2="{BOARD_W}" y2="{y}" stroke="#dde3dd"/>')
        o.append(f'<text x="-6" y="{y + 4}" font-size="11" fill="#777" text-anchor="end">{gy}</text>')
    cx, cy = px(HOLE[0], HOLE[1])
    o.append(f'<circle cx="{cx}" cy="{cy}" r="{HOLE[2] * S}" fill="#fff" stroke="#222" stroke-width="2"/>'
             f'<text x="{cx}" y="{cy + 4}" font-size="10" fill="#555" text-anchor="middle">trou Ø6</text>')
    # connecteurs (face arriere du PCB) et JTAG (face avant) : reperes seulement
    for lab, x, y, back in (("MC2", 156, 92, True), ("MC3", 185, 67.5, True), ("MC1", 214, 92, True), ("J2 JTAG", 141.2, 58.5, False)):
        a, b = px(x, y)
        o.append(f'<text x="{a}" y="{b}" font-size="12" fill="#888" text-anchor="middle">{lab}{" (module XEM, face arrière)" if back and not mirrored else ""}'
                 f'{" (face avant, caché)" if (not back and mirrored) else ""}</text>')
    w, h = MIC_W * S, MIC_H * S
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
    open(os.path.join(HERE, "mic_layout_acoustique.svg"), "w").write(render(m, True))
    open(os.path.join(HERE, "mic_layout_composants.svg"), "w").write(render(m, False))
    print("mic_layout_acoustique.svg, mic_layout_composants.svg")
