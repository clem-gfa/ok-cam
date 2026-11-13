#!/bin/sh
# Simulation iverilog du FIR, verification bit-exact. Usage : ./run_fir.sh
set -e
cd "$(dirname "$0")"
python3 - <<'PY'
import numpy as np, pdm_chain as m
N = 300
rng = np.random.default_rng(1)
t = np.arange(N * m.CIC_R) / m.FPDM
cols = []
for ch in range(64):
    f0 = 1e3 + ch * 1.2e3
    bits = (m.sigma_delta2(0.5 * np.sin(2 * np.pi * f0 * t)) > 0).astype(np.int64) * 2 - 1
    cols.append(m.cic_decimate(bits)[:N])
cols = np.array(cols).T                      # N x 64
cols[0] = np.clip(rng.integers(-(1 << 17), 1 << 17, 64), -250000, 250000)   # cas extreme sur la 1re ligne
cols[:, 63] = 248832 * (1 - 2 * (np.arange(N) % 2))   # pire cas : pleine echelle a Nyquist
with open("cic_in.hex", "w") as f:
    for n in range(N):
        f.write("%0304x\n" % sum((int(cols[n, ch]) & 0x7FFFF) << (19 * ch) for ch in range(64)))
PY
iverilog -g2012 -I ../rtl -P tb_fir.N=300 -o tbf.vvp tb_fir.v ../rtl/fir_bank.v
vvp tbf.vvp | tail -2
python3 check_fir.py 300
