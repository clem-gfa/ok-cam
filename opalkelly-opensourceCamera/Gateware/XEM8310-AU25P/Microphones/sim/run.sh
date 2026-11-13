#!/bin/sh
# Simulation iverilog + verification bit-exact. Usage : ./run.sh [SD] [INVERT]
set -e
cd "$(dirname "$0")"
SD=${1:-7}; INV=${2:-0}
python3 gen_vectors.py 3000
iverilog -g2012 -P tb_mic_front.SD=$SD -P tb_mic_front.INVERT=$INV -o tb.vvp tb_mic_front.v ../rtl/mic_clkgen.v ../rtl/pdm_capture.v ../rtl/cic_bank.v
vvp tb.vvp | tail -3
python3 check_chain.py 3000
