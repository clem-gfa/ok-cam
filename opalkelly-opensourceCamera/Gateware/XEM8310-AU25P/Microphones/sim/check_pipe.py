#!/usr/bin/env python3
"""Verifie le flux de la pipe 0xA1 : paquets valides, seq continue, et echantillons == modele (CIC puis FIR entiers)."""
import sys
import numpy as np
import pdm_chain as m
words = [int(x, 16) for x in open("pipe_words.txt").read().split()]
pk = [words[i:i + 1024] for i in range(0, len(words) - len(words) % 1024, 1024)]
dump = [int(x, 16) for x in open("pdm_dump.txt").read().split()]
print(f"{len(words)} mots lus = {len(pk)} paquets complets")
ok = len(pk) >= 3
for i, p in enumerate(pk):
    if p[0] != 0x4D494331 or p[1] != i or (p[4] & 0xFFFF) != 21 or (p[4] >> 16) & 1:
        print("en-tete incorrect, paquet", i, hex(p[0]), p[1], hex(p[4])); ok = False
# instants recus : 64 canaux x 24 bits dans 48 mots
inst = []
for p in pk:
    for t in range(21):
        v = 0
        for w in range(48): v |= p[8 + t * 48 + w] << (32 * w)
        inst.append([((v >> (24 * c)) & 0xFFFFFF) for c in range(64)])
inst = np.array(inst)
inst = np.where(inst >= 1 << 23, inst - (1 << 24), inst)
bad = 0
for c in range(64):
    bits = np.array([(d >> c) & 1 for d in dump]) * 2 - 1
    ref = m.fir_int(m.cic_decimate(bits))
    n = min(len(ref), len(inst))
    if n < 40 or not np.array_equal(ref[:n], inst[:n, c]): bad += 1
print(f"echantillons : {64 - bad}/64 canaux identiques au modele (sur {len(inst)} instants recus)")
sys.exit(0 if ok and bad == 0 else 1)
