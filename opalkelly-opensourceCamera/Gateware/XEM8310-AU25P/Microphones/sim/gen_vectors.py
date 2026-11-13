#!/usr/bin/env python3
"""Genere pdm_src.hex : 64 canaux PDM (tons de 1 a 77 kHz) via le modulateur sigma-delta du modele."""
import sys
import numpy as np
import pdm_chain as m

L = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
t = np.arange(L) / m.FPDM
bits = np.zeros((L, 64), dtype=np.uint8)
for c in range(64):
    f0 = 1e3 + c * 1.2e3
    bits[:, c] = (m.sigma_delta2(0.5 * np.sin(2 * np.pi * f0 * t)) > 0)
with open("pdm_src.hex", "w") as f:
    for n in range(L):
        f.write("%016x\n" % sum(int(bits[n, c]) << c for c in range(64)))
