#----------------------------------------------------------------------
# add_lp_ila.tcl - bring-up only: ILA on the MIPI D-PHY RX low-power receivers
#
# Puts an ILA (16384 samples @ ~200 MHz = ~80 us per capture) on the outputs of the three
# IBUFDS_DPHY input buffers of the MIPI CSI-2 RX subsystem:
#   lp_rx_o_p[2:0], lp_rx_o_n[2:0]  : LP receiver outputs (index 0 = clock lane, 1 = data lane 0,
#                                     2 = data lane 1)
#   hs_rx_disable[2:0]              : HS receiver disable, driven by the D-PHY lane state machines
# It shows what the FPGA input buffers really see on the lanes (LP-11 -> LP-01 -> LP-00 -> HS),
# independently of the D-PHY state machines whose status registers stay frozen.
#
# Usage, in the Vivado Tcl console, with the project open (e.g. C:/_DW/cam8310fix/Camera.xpr)
# and synthesis complete (synth_1 up to date):
#   source add_lp_ila.tcl
#   reset_run impl_1
#   launch_runs impl_1 -to_step write_bitstream -jobs 1
#   wait_on_runs impl_1
# The probes file is C:/_DW/lp_ila.ltx. The debug core is saved in a separate constraints file
# (C:/_DW/lp_ila.xdc), so the repository XDC is not modified.
#----------------------------------------------------------------------

set lp_ila_dir "C:/_DW"
file mkdir $lp_ila_dir

open_run synth_1 -name synth_1

set base "Camera_i/mipi_csi2_rx_subsyst_0/inst/phy/inst/inst/bd_8c57_phy_0_rx_support_i/slave_rx.bd_8c57_phy_0_rx_hssio_i/inst/top_inst/iobuf_top_inst"
set probes {}
foreach sig {lp_rx_o_p lp_rx_o_n hs_rx_disable} {
    foreach i {0 1 2} {
        set n [get_nets -quiet -hier -filter "NAME == $base/${sig}\[$i\]"]
        if {$n eq ""} { error "net not found: $base/${sig}\[$i\] (the IP hierarchy name may differ: list the IBUFDS_DPHY cells to adapt \$base)" }
        lappend probes $n
    }
}

# ILA clock: the 200 MHz clock of the D-PHY (output of clk_wiz_0, after its global buffer).
set clk [get_nets -quiet -of_objects [get_pins Camera_i/clk_wiz_0/clk_out1]]
if {$clk eq ""} { error "clock net not found (Camera_i/clk_wiz_0/clk_out1)" }

# Keep the debug core in its own constraints file.
set dbg_xdc "$lp_ila_dir/lp_ila.xdc"
close [open $dbg_xdc w]
add_files -fileset constrs_1 -norecurse $dbg_xdc
set_property target_constrs_file $dbg_xdc [current_fileset -constrset]

create_debug_core u_ila_lp ila
set_property C_DATA_DEPTH 16384 [get_debug_cores u_ila_lp]
set_property C_TRIGIN_EN false [get_debug_cores u_ila_lp]
set_property C_TRIGOUT_EN false [get_debug_cores u_ila_lp]
set_property C_ADV_TRIGGER false [get_debug_cores u_ila_lp]
set_property C_INPUT_PIPE_STAGES 0 [get_debug_cores u_ila_lp]
set_property C_EN_STRG_QUAL false [get_debug_cores u_ila_lp]
set_property ALL_PROBE_SAME_MU true [get_debug_cores u_ila_lp]
set_property ALL_PROBE_SAME_MU_CNT 1 [get_debug_cores u_ila_lp]
set_property port_width 1 [get_debug_ports u_ila_lp/clk]
connect_debug_port u_ila_lp/clk $clk
set_property port_width 9 [get_debug_ports u_ila_lp/probe0]
connect_debug_port u_ila_lp/probe0 $probes

save_constraints -force
write_debug_probes -force "$lp_ila_dir/lp_ila.ltx"
puts "ILA added. Now: reset_run impl_1; launch_runs impl_1 -to_step write_bitstream -jobs 1"
