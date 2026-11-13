#!/bin/sh
# Test de bout en bout du sous-systeme micros (micros -> pipe 0xA1). Usage : ./run_subsystem.sh
set -e
cd "$(dirname "$0")"
python3 gen_vectors.py 3000
iverilog -g2012 -I ../rtl -o tbs.vvp tb_mic_subsystem.v fifo_audio_pkt_model.v ../rtl/mic_subsystem.v ../rtl/mic_clkgen.v ../rtl/pdm_capture.v ../rtl/cic_bank.v ../rtl/fir_bank.v ../rtl/audio_packetizer.v ../rtl/ts_unit.v
vvp tbs.vvp | tail -2
python3 check_pipe.py
