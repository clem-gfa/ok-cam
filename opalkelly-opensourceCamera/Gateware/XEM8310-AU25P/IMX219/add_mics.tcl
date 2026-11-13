#----------------------------------------------------------------------
# add_mics.tcl - ajoute les 64 micros PDM (SPH0641LU4H-1) au projet camera IMX219.
#
# NON TESTE sous Vivado (ecrit sans acces a l'outil) : les RTL sont verifies en simulation
# (Microphones/sim/run_subsystem.sh), mais ce script de block design peut demander des corrections
# (noms de pins d'interface du FrontPanel, parametres du FIFO IP).
#
# Utilisation : activer le drapeau avant de sourcer project.tcl :
#     set with_mics 1
#     source project.tcl
# project.tcl appelle ce script juste avant la creation du wrapper. Par defaut (drapeau absent) le
# projet camera est strictement inchange.
#
# Ajoute : sous-systeme micros (module mic_subsystem), PLL micros 95,5 MHz (clk_wiz_mic) depuis okClk,
# FIFO audio a horloges independantes, WireIn 0x01, WireOut 0x20-0x22, pipe a blocs 0xA1, ports mic_clk / mic_data[31:0],
# contraintes mic_pins.xdc (+ VIO3 = 1,8 V a regler cote FrontPanel) et mic_timing.xdc.
#----------------------------------------------------------------------

set mic_rtl ../Microphones/rtl

# --- sources -------------------------------------------------------------------------------
add_files -norecurse [list \
    $mic_rtl/mic_clkgen.v $mic_rtl/pdm_capture.v $mic_rtl/cic_bank.v $mic_rtl/fir_bank.v \
    $mic_rtl/audio_packetizer.v $mic_rtl/ts_unit.v $mic_rtl/mic_subsystem.v $mic_rtl/fir_coeffs.vh]
set_property file_type {Verilog Header} [get_files fir_coeffs.vh]
set_property is_global_include true [get_files fir_coeffs.vh]
set_property include_dirs [list [file normalize $mic_rtl]] [current_fileset]

# FIFO audio : horloges independantes (ecriture = horloge micros, lecture = okClk), 4096 x 32, non FWFT
create_ip -name fifo_generator -vendor xilinx.com -library ip -version 13.2 -module_name fifo_audio_pkt
set_property -dict [list \
  CONFIG.Fifo_Implementation {Independent_Clocks_Block_RAM} \
  CONFIG.Input_Data_Width {32} \
  CONFIG.Input_Depth {4096} \
  CONFIG.Output_Data_Width {32} \
  CONFIG.Reset_Pin {true} \
  CONFIG.Use_Embedded_Registers {false} \
  CONFIG.Read_Data_Count {true} \
  CONFIG.Write_Data_Count {true} \
  CONFIG.Read_Data_Count_Width {13} \
  CONFIG.Write_Data_Count_Width {13} \
] [get_ips fifo_audio_pkt]

add_files -fileset constrs_1 -norecurse [list ../Microphones/mic_pins.xdc ../Microphones/mic_timing.xdc]
update_compile_order -fileset sources_1

# --- FrontPanel : endpoints supplementaires ------------------------------------------------------
set_property -dict [list \
    CONFIG.WI.ADDR_0 {0x00} CONFIG.WI.ADDR_1 {0x01} CONFIG.WI.ADDR_2 {0x1d} CONFIG.WI.ADDR_3 {0x1e} CONFIG.WI.ADDR_4 {0x1f} \
    CONFIG.WI.COUNT {5} \
    CONFIG.WO.ADDR_0 {0x20} CONFIG.WO.ADDR_1 {0x21} CONFIG.WO.ADDR_2 {0x22} CONFIG.WO.ADDR_3 {0x3e} CONFIG.WO.ADDR_4 {0x3f} \
    CONFIG.WO.COUNT {5} \
    CONFIG.BTPO.ADDR_0 {0xA0} CONFIG.BTPO.ADDR_1 {0xA1} \
    CONFIG.BTPO.COUNT {2} \
] [get_bd_cells frontpanel_0]

# --- PLL micros : 95,5 MHz depuis okClk (100,8 MHz) -> horloge PDM 4,775 MHz (< 4,8 MHz maxi du micro) -----
set clk_wiz_mic [ create_bd_cell -type ip -vlnv xilinx.com:ip:clk_wiz clk_wiz_mic ]
set_property -dict [list \
    CONFIG.PRIM_IN_FREQ {100.8} \
    CONFIG.PRIM_SOURCE {No_buffer} \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ {95.5} \
    CONFIG.USE_LOCKED {true} \
    CONFIG.USE_RESET {false} \
] $clk_wiz_mic

# --- sous-systeme micros -----------------------------------------------------------------------
set block_name mic_subsystem
if { [catch {set mic_subsystem_0 [create_bd_cell -type module -reference $block_name mic_subsystem_0] } errmsg] } {
   error "Unable to add referenced block mic_subsystem: $errmsg"
}

# SOF video : a brancher sur un detecteur de debut de trame du flux camera (non fait : tenu a 0, le
# compteur vcnt de l'en-tete reste a 0). Voir Microphones/README.md.
set vsof_zero [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant vsof_zero ]
set_property -dict [list CONFIG.CONST_VAL {0} CONFIG.CONST_WIDTH {1}] $vsof_zero

# Ports externes
set mic_clk  [ create_bd_port -dir O mic_clk ]
set mic_data [ create_bd_port -dir I -from 31 -to 0 mic_data ]

# Interfaces FrontPanel
connect_bd_intf_net [get_bd_intf_pins frontpanel_0/wirein01]  [get_bd_intf_pins mic_subsystem_0/wirein01_mic_ctrl]
connect_bd_intf_net [get_bd_intf_pins frontpanel_0/wireout20] [get_bd_intf_pins mic_subsystem_0/wireout20_mic_status]
connect_bd_intf_net [get_bd_intf_pins frontpanel_0/wireout21] [get_bd_intf_pins mic_subsystem_0/wireout21_mic_dropped]
connect_bd_intf_net [get_bd_intf_pins frontpanel_0/wireout22] [get_bd_intf_pins mic_subsystem_0/wireout22_mic_overruns]
connect_bd_intf_net [get_bd_intf_pins frontpanel_0/btpipeouta1] [get_bd_intf_pins mic_subsystem_0/btpipeout_audio]

# Horloges
connect_bd_net [get_bd_pins frontpanel_0/okClk] [get_bd_pins clk_wiz_mic/clk_in1] [get_bd_pins mic_subsystem_0/ok_clk]
connect_bd_net [get_bd_pins clk_wiz_mic/clk_out1] [get_bd_pins mic_subsystem_0/clk]
connect_bd_net [get_bd_pins clk_wiz_mic/locked]   [get_bd_pins mic_subsystem_0/locked]
connect_bd_net [get_bd_pins clk_wiz_0/clk_out2]   [get_bd_pins mic_subsystem_0/vclk]
connect_bd_net [get_bd_pins vsof_zero/dout]       [get_bd_pins mic_subsystem_0/vsof]

# Pads
connect_bd_net [get_bd_pins mic_subsystem_0/mic_clk_o]  [get_bd_ports mic_clk]
connect_bd_net [get_bd_ports mic_data]                  [get_bd_pins mic_subsystem_0/mic_data_i]

puts "add_mics.tcl : sous-systeme micros ajoute. Rappels : VIO3 = 1,8 V (FrontPanel Device Manager), mic_pins.xdc en LVCMOS18."
