#!/usr/bin/env python3
"""Compare les sorties du banc RTL avec le modele (cic_decimate de pdm_chain.py)."""
import sys
import numpy as np
import pdm_chain as m

L = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
CH = 64
src = [int(x, 16) for x in open("pdm_src.hex").read().split()]
dump = [int(x, 16) for x in open("pdm_dump.txt").read().split()]
out = np.array([[int(v) for v in l.split()] for l in open("cic_out.txt") if l.strip()])

# 1) alignement : le flux capture doit etre exactement la source decalee de k periodes
ok_k = None
for k in range(0, 8):
    n = min(len(dump), L - k)
    if all(dump[i] == src[(i + k) % L] for i in range(n)):
        ok_k = k; break
print(f"capture PDM : {'OK, decalage ' + str(ok_k) + ' periode(s)' if ok_k is not None else 'ECHEC (flux different de la source)'}")

# 2) CIC bit-exact sur le flux capture
bad = 0
for c in range(CH):
    bits = np.array([(d >> c) & 1 for d in dump]) * 2 - 1
    ref = m.cic_decimate(bits)
    n = min(len(ref), len(out))
    if not np.array_equal(ref[:n], out[:n, c]):
        bad += 1
print(f"CIC : {CH - bad}/{CH} canaux identiques au modele ({out.shape[0]} sorties par canal)")
sys.exit(0 if (ok_k is not None and bad == 0) else 1)
