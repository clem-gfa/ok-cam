#!/usr/bin/env python3
"""Compare fir_out.txt (RTL) avec le modele entier fir_int() sur les memes entrees (cic_in.hex)."""
import sys
import numpy as np
import pdm_chain as m
N = int(sys.argv[1]) if len(sys.argv) > 1 else 300
W = 19
words = [int(x, 16) for x in open("cic_in.hex").read().split()]
def s(v): return v - (1 << W) if v >> (W - 1) else v
c = np.array([[s((w >> (W * ch)) & ((1 << W) - 1)) for ch in range(64)] for w in words])
out = np.array([[int(v) for v in l.split()] for l in open("fir_out.txt") if l.strip()])
coef = m.fir_q17()
bad = 0
for ch in range(64):
    ref = m.fir_int(c[:, ch], coef)
    n = min(len(ref), len(out))
    if n < 100 or not np.array_equal(ref[:n], out[:n, ch]):
        bad += 1
        if bad <= 3:
            d = np.nonzero(ref[:n] != out[:n, ch])[0]
            print("canal", ch, "premiere difference a l'indice", d[:3], ref[d[:1]], out[d[:1], ch])
print(f"FIR : {64 - bad}/64 canaux identiques au modele ({out.shape[0]} sorties, entree {len(c)} echantillons)")
sys.exit(0 if bad == 0 else 1)
