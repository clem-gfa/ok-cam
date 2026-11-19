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

# Passages entre horloges des synchroniseurs a deux bascules (ASYNC_REG) de mic_subsystem.v : go/gd (compteurs d'etat), s1
# (ultra, run, locked, full_seen), c1 (registre de commande), idl, sp, rq, gr1. Chaque premiere bascule est echantillonnee
# par une horloge differente de celle de sa source : voulu. Mesure sur le build du 19/11 (report_timing_summary) : ces premiers
# etages etaient les seuls chemins en violation (WNS -3,35 ns, 110 points), tous des chemins inter-horloges chronometres comme
# synchrones avec une exigence de 0,08 a 0,3 ns (horloges 99,96 et 100,8 MHz non multiples, ou ui_clk 300 MHz). Le groupe
# asynchrone ci-dessus ne couvre que les deux familles ; clk_out1/clk_out2 de clk_wiz_0 et okClk sont dans la meme famille
# (meme MMCM), d'ou ces exceptions explicites sur les cellules ASYNC_REG de mic_subsystem.
set_false_path -to [get_cells -quiet -hierarchical -filter {ASYNC_REG == TRUE && NAME =~ *mic_subsystem_0/inst/*}]
