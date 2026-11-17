# Familles d'horloges asynchrones du design avec micros.
#
# Le chemin DDR4 des micros (anneau audio) traverse deux familles d'horloges sans horloge primaire commune :
#   - la famille de la DDR4 : primaire `refclk` (ddr4_refclk) -> mmcm_clkout0 (horloge utilisateur, 300 MHz), mmcm_clkout5/6, pll_clk[*]
#   - la famille FrontPanel : primaire `okUH0` -> mmcm0_clk0 (okClk) -> clk_out1/clk_out2 de clk_wiz_0 (99,96 MHz : micros, video)
# Tous les passages entre ces familles sont surs par construction : FIFO XPM (pointeurs en code de Gray, contraintes
# propres aux macros) et synchroniseurs a deux bascules ASYNC_REG (mic_subsystem.v : rq, idl, sp, gr1/gr2). Sans cette
# declaration, Vivado les chronometre comme des chemins synchrones (TIMING-6/7/8/51, avertissements critiques).
# Appliquee en implementation seulement (USED_IN_SYNTHESIS false, voir add_mics.tcl).
set_clock_groups -asynchronous \
    -group [get_clocks -quiet -include_generated_clocks refclk] \
    -group [get_clocks -quiet -include_generated_clocks okUH0]
