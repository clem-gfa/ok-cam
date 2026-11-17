#!/bin/sh
# Test de bout en bout avec l'anneau en DDR4 (modele AXI) : le « PC » s'arrete ~12 paquets, aucun paquet ne doit etre perdu.
set -e
cd "$(dirname "$0")"
python3 gen_vectors.py 3000
iverilog -g2012 -I ../rtl -o tbd.vvp tb_mic_subsystem_ddr.v fifo_ddr_models.v fifo_audio_pkt_model.v ../rtl/mic_subsystem.v ../rtl/mic_clkgen.v ../rtl/pdm_capture.v ../rtl/cic_bank.v ../rtl/fir_bank.v ../rtl/audio_packetizer.v ../rtl/ts_unit.v ../rtl/audio_ddr_ring.v
vvp tbd.vvp | tail -3
python3 check_pipe.py
