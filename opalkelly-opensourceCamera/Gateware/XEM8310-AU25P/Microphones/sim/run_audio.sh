#!/bin/sh
# Simulation du paquetiseur audio + base de temps. Usage : ./run_audio.sh
set -e
cd "$(dirname "$0")"
iverilog -g2012 -o tba.vvp tb_audio_pkt.v ../rtl/audio_packetizer.v ../rtl/ts_unit.v
vvp tba.vvp | tail -2
python3 check_audio.py
