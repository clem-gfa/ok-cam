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
# Ajoute : sous-systeme micros (module mic_subsystem), horloge micros 99,96 MHz (clk_wiz_0/clk_out2),
# FIFO audio a horloges independantes, WireIn 0x01, WireOut 0x20-0x24, pipe a blocs 0xA1, ports mic_clk / mic_data[31:0],
# contraintes mic_pins.xdc (+ VIO3 = 1,8 V a regler cote FrontPanel) et mic_timing.xdc.
#----------------------------------------------------------------------

set mic_rtl ../Microphones/rtl

# Profondeur du FIFO audio = 2^mic_fifo_aw mots de 32 bits. 15 (defaut) = 128 Kio = ~3,6 ms de flux audio ;
# 16 = 256 Kio (~7 ms), 17 = 512 Kio (~14 ms). Plus c'est profond, plus l'appli PC peut etre lente (un FIFO qui
# deborde fait perdre des paquets entiers) mais plus ca consomme de BRAM : verifier l'utilisation apres synthese.
if {![info exists mic_fifo_aw]} { set mic_fifo_aw 15 }

# Tampon en anneau en DDR4 (defaut : oui). Les paquets audio passent par une zone de la DDR4 (64 Mio a
# 0xA0000000 avec mic_ring_aw = 14 : 2^14 paquets de 4 Kio, ~1,8 s de flux) entre deux petits FIFO : l'appli PC peut
# alors etre bloquee longtemps sans perdre de paquets. set mic_use_ddr 0 : ancien chemin, un seul FIFO (mic_fifo_aw).
if {![info exists mic_use_ddr]} { set mic_use_ddr 1 }
if {![info exists mic_ring_aw]} { set mic_ring_aw 14 }

# --- sources -------------------------------------------------------------------------------
add_files -norecurse [list \
    $mic_rtl/mic_clkgen.v $mic_rtl/pdm_capture.v $mic_rtl/cic_bank.v $mic_rtl/fir_bank.v \
    $mic_rtl/audio_packetizer.v $mic_rtl/ts_unit.v $mic_rtl/mic_subsystem.v $mic_rtl/audio_ddr_ring.v $mic_rtl/fifo_audio_stage.v $mic_rtl/fifo_audio_out.v $mic_rtl/axis_sof_monitor.v $mic_rtl/fir_coeffs.vh]
set_property file_type {Verilog Header} [get_files fir_coeffs.vh]
set_property is_global_include true [get_files fir_coeffs.vh]
set_property include_dirs [list [file normalize $mic_rtl]] [current_fileset]

if {$mic_use_ddr} {
    # Les deux FIFO du chemin DDR4 (32 -> 128 bits et 128 -> 32 bits, horloges independantes) sont des macros XPM
    # (rtl/fifo_audio_stage.v, rtl/fifo_audio_out.v) : l'IP FIFO Generator de cette version refuse une largeur de
    # lecture differente de la largeur d'ecriture par script (Output_Data_Width desactive). Rien a configurer ici.
    set_property XPM_LIBRARIES {XPM_FIFO} [current_project]
} else {
# FIFO audio : horloges independantes (ecriture = horloge micros, lecture = okClk), 2^mic_fifo_aw x 32, non FWFT.
# Profond a dessein : l'appli lit la video par tranches et ne lit l'audio qu'entre deux tranches.
create_ip -name fifo_generator -vendor xilinx.com -library ip -version 13.2 -module_name fifo_audio_pkt
set_property -dict [list \
  CONFIG.Fifo_Implementation {Independent_Clocks_Block_RAM} \
  CONFIG.Input_Data_Width {32} \
  CONFIG.Input_Depth [expr {1 << $mic_fifo_aw}] \
  CONFIG.Output_Data_Width {32} \
  CONFIG.Reset_Pin {true} \
  CONFIG.Use_Embedded_Registers {false} \
  CONFIG.Read_Data_Count {true} \
  CONFIG.Write_Data_Count {true} \
  CONFIG.Read_Data_Count_Width $mic_fifo_aw \
  CONFIG.Write_Data_Count_Width $mic_fifo_aw \
] [get_ips fifo_audio_pkt]

}

add_files -fileset constrs_1 -norecurse [list ../Microphones/mic_pins.xdc ../Microphones/mic_timing.xdc]
if {$mic_use_ddr} {
    # le chemin DDR4 traverse deux familles d'horloges sans primaire commune (refclk / okUH0) : voir le fichier
    add_files -fileset constrs_1 -norecurse ../Microphones/mic_cdc.xdc
    set_property USED_IN_SYNTHESIS false [get_files mic_cdc.xdc]
}
update_compile_order -fileset sources_1

# --- FrontPanel : endpoints supplementaires ------------------------------------------------------
set_property -dict [list \
    CONFIG.WI.ADDR_0 {0x00} CONFIG.WI.ADDR_1 {0x01} CONFIG.WI.ADDR_2 {0x1d} CONFIG.WI.ADDR_3 {0x1e} CONFIG.WI.ADDR_4 {0x1f} \
    CONFIG.WI.COUNT {5} \
    CONFIG.WO.ADDR_0 {0x20} CONFIG.WO.ADDR_1 {0x21} CONFIG.WO.ADDR_2 {0x22} CONFIG.WO.ADDR_3 {0x23} CONFIG.WO.ADDR_4 {0x24} CONFIG.WO.ADDR_5 {0x3e} CONFIG.WO.ADDR_6 {0x3f} \
    CONFIG.WO.COUNT {7} \
    CONFIG.BTPO.ADDR_0 {0xA0} CONFIG.BTPO.ADDR_1 {0xA1} \
    CONFIG.BTPO.COUNT {2} \
] [get_bd_cells frontpanel_0]

# --- Horloge micros : on reutilise clk_wiz_0/clk_out2 (99,96 MHz). Un second MMCM sur okClk ne place pas
# (cascade MMCM -> BUFGCE -> 2 MMCM, Place 30-718). 99,96 MHz / 22 = 4,544 MHz (maxi du micro : 4,8 MHz).
# clk_wiz_0 n'a pas de sortie 'locked' : le signal locked du sous-systeme est tenu a 1.
set mic_locked [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant mic_locked ]
set_property -dict [list CONFIG.CONST_VAL {1} CONFIG.CONST_WIDTH {1}] $mic_locked

# --- sous-systeme micros -----------------------------------------------------------------------
set block_name mic_subsystem
if { [catch {set mic_subsystem_0 [create_bd_cell -type module -reference $block_name mic_subsystem_0] } errmsg] } {
   error "Unable to add referenced block mic_subsystem: $errmsg"
}
set_property CONFIG.FIFO_AW $mic_fifo_aw [get_bd_cells mic_subsystem_0]
set_property CONFIG.USE_DDR $mic_use_ddr [get_bd_cells mic_subsystem_0]

# Debut de trame video : detecteur en observateur (interface AXI-Stream « monitor ») sur le flux qui entre
# dans le VDMA (v_tpg_0 -> axi_vdma_0/S_AXIS_S2MM). Si le branchement de l'observateur echoue dans votre
# version de Vivado, le SOF est tenu a 0 (l'audio fonctionne, mais vcnt/vts de l'en-tete restent a 0).
set sof_ok 0
if { [catch {
    set sof_mon [ create_bd_cell -type module -reference axis_sof_monitor axis_sof_monitor_0 ]
    connect_bd_intf_net [get_bd_intf_pins axi_vdma_0/S_AXIS_S2MM] [get_bd_intf_pins axis_sof_monitor_0/mon]
    connect_bd_net [get_bd_pins clk_wiz_0/clk_out2] [get_bd_pins axis_sof_monitor_0/aclk]
    connect_bd_net [get_bd_pins axis_sof_monitor_0/sof] [get_bd_pins mic_subsystem_0/vsof]
    set sof_ok 1
} sof_err ] } {
    puts "WARNING add_mics.tcl : detecteur de SOF non branche ($sof_err) ; vsof tenu a 0."
    catch {delete_bd_objs [get_bd_cells axis_sof_monitor_0]}
    set vsof_zero [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant vsof_zero ]
    set_property -dict [list CONFIG.CONST_VAL {0} CONFIG.CONST_WIDTH {1}] $vsof_zero
    connect_bd_net [get_bd_pins vsof_zero/dout] [get_bd_pins mic_subsystem_0/vsof]
}

# Ports externes
set mic_clk  [ create_bd_port -dir O mic_clk ]
set mic_data [ create_bd_port -dir I -from 31 -to 0 mic_data ]

# Interfaces FrontPanel
connect_bd_intf_net [get_bd_intf_pins frontpanel_0/wirein01]  [get_bd_intf_pins mic_subsystem_0/wirein01_mic_ctrl]
connect_bd_intf_net [get_bd_intf_pins frontpanel_0/wireout20] [get_bd_intf_pins mic_subsystem_0/wireout20_mic_status]
connect_bd_intf_net [get_bd_intf_pins frontpanel_0/wireout21] [get_bd_intf_pins mic_subsystem_0/wireout21_mic_dropped]
connect_bd_intf_net [get_bd_intf_pins frontpanel_0/wireout22] [get_bd_intf_pins mic_subsystem_0/wireout22_mic_overruns]
connect_bd_intf_net [get_bd_intf_pins frontpanel_0/wireout23] [get_bd_intf_pins mic_subsystem_0/wireout23_mic_level]
connect_bd_intf_net [get_bd_intf_pins frontpanel_0/wireout24] [get_bd_intf_pins mic_subsystem_0/wireout24_mic_ring]
connect_bd_intf_net [get_bd_intf_pins frontpanel_0/btpipeouta1] [get_bd_intf_pins mic_subsystem_0/btpipeout_audio]

# Horloges
connect_bd_net [get_bd_pins frontpanel_0/okClk]  [get_bd_pins mic_subsystem_0/ok_clk]
connect_bd_net [get_bd_pins clk_wiz_0/clk_out2]  [get_bd_pins mic_subsystem_0/clk] [get_bd_pins mic_subsystem_0/vclk]
connect_bd_net [get_bd_pins mic_locked/dout]     [get_bd_pins mic_subsystem_0/locked]

# Anneau en DDR4 : le maitre AXI est un module de reference SEPARE (audio_ddr_ring) a UNE seule horloge (celle de la DDR4).
# Dans un module a plusieurs horloges, Vivado associait le bus m_axi a deux horloges a la fois (BD 41-1732) puis donnait
# a l'interface la mauvaise frequence (BD 41-237). Il devient un maitre AXI de plus sur smartconnect_0 (S02_AXI).
if {$mic_use_ddr} {
    set audio_ddr_ring_0 [ create_bd_cell -type module -reference audio_ddr_ring audio_ddr_ring_0 ]
    set_property CONFIG.RING_AW $mic_ring_aw [get_bd_cells audio_ddr_ring_0]
    set_property CONFIG.NUM_SI {3} [get_bd_cells smartconnect_0]
    connect_bd_intf_net [get_bd_intf_pins audio_ddr_ring_0/m_axi] [get_bd_intf_pins smartconnect_0/S02_AXI]
    connect_bd_net [get_bd_pins ddr4_0/c0_ddr4_ui_clk]   [get_bd_pins audio_ddr_ring_0/clk] [get_bd_pins mic_subsystem_0/ui_clk]
    connect_bd_net [get_bd_pins util_vector_logic_0/Res] [get_bd_pins audio_ddr_ring_0/aresetn]
    # signaux simples entre le sous-systeme (FIFO) et l'anneau
    foreach {src dst} {
        mic_subsystem_0/ring_stg_dout     audio_ddr_ring_0/stg_dout
        mic_subsystem_0/ring_stg_empty    audio_ddr_ring_0/stg_empty
        mic_subsystem_0/ring_stg_avail    audio_ddr_ring_0/stg_avail
        audio_ddr_ring_0/stg_rd_en        mic_subsystem_0/ring_stg_rd_en
        audio_ddr_ring_0/out_din          mic_subsystem_0/ring_out_din
        audio_ddr_ring_0/out_wr_en        mic_subsystem_0/ring_out_wr_en
        mic_subsystem_0/ring_out_full     audio_ddr_ring_0/out_full
        mic_subsystem_0/ring_out_wr_count audio_ddr_ring_0/out_wr_count
        mic_subsystem_0/ring_rst_req      audio_ddr_ring_0/rst_req
        audio_ddr_ring_0/idle             mic_subsystem_0/ring_idle
        audio_ddr_ring_0/space_ok         mic_subsystem_0/ring_space_ok
        audio_ddr_ring_0/used_pkts        mic_subsystem_0/ring_used
        audio_ddr_ring_0/peak_pkts        mic_subsystem_0/ring_peak
    } {
        connect_bd_net [get_bd_pins $src] [get_bd_pins $dst]
    }
    assign_bd_address -offset 0x80000000 -range $ddr4_range -target_address_space [get_bd_addr_spaces audio_ddr_ring_0/m_axi] [get_bd_addr_segs ddr4_0/C0_DDR4_MEMORY_MAP/C0_DDR4_ADDRESS_BLOCK] -force
}

# Pads
connect_bd_net [get_bd_pins mic_subsystem_0/mic_clk_o]  [get_bd_ports mic_clk]
connect_bd_net [get_bd_ports mic_data]                  [get_bd_pins mic_subsystem_0/mic_data_i]

puts "add_mics.tcl : sous-systeme micros ajoute. Rappels : VIO3 = 1,8 V (FrontPanel Device Manager), mic_pins.xdc en LVCMOS18."
