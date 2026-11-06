#----------------------------------------------------------------------
# project.tcl
#
# Project creation script.
#
# Copyright (c) 2025 Opal Kelly Incorporated
#----------------------------------------------------------------------

#--------------------------------------------------------------------------
# Check FrontPanel Vivado IP Core path
#--------------------------------------------------------------------------

if {![info exists fpdir]} {
    puts "ERROR: You must set the 'fpdir' variable before running this script."
    puts "Use the command: 'set fpdir <dir>'"
    puts "This variable should point to the FrontPanel Vivado IP Core distribution."
    error "Missing FrontPanel Vivado IP Core path."
}

#----------------------------------------------------------------------
# Project Creation
#----------------------------------------------------------------------

set project_name "Camera"
set project_dir  "Vivado"

start_gui
create_project $project_name $project_dir -part xcau25p-ffvb676-2-e

# Add HDL sources
add_files -norecurse { \
    syzygy_camera_phy.v \
    axi_reset_iwrap.v \
    fp_to_axil.v \
    fp_to_axil_iwrap.v \
    axis_to_btpipeout_shim.v \
}

# Add constraints
add_files -fileset constrs_1 -norecurse xem8320-au25p.xdc

create_ip -name fifo_generator -vendor xilinx.com -library ip -version 13.2 -module_name fifo_axis_to_btpipeout_shim
set_property -dict [list \
  CONFIG.Fifo_Implementation {Common_Clock_Block_RAM} \
  CONFIG.Full_Threshold_Assert_Value {256} \
  CONFIG.Input_Data_Width {32} \
  CONFIG.Input_Depth {512} \
  CONFIG.Programmable_Full_Type {Single_Programmable_Full_Threshold_Constant} \
  CONFIG.Reset_Pin {true} \
  CONFIG.Use_Embedded_Registers {false} \
] [get_ips fifo_axis_to_btpipeout_shim]

#----------------------------------------------------------------------
# Set IP Repositories
#----------------------------------------------------------------------
set ip_paths {}
lappend ip_paths \
[list ./HLS] \
$fpdir

set_property ip_repo_paths $ip_paths [current_project]
update_ip_catalog

#----------------------------------------------------------------------
# Create proc_sys_reset IP (required by axi_reset_iwrap.v)
#----------------------------------------------------------------------
create_ip -name proc_sys_reset -vendor xilinx.com -library ip -version 5.0 -module_name proc_sys_reset_0

#----------------------------------------------------------------------
# Create Block Design
#----------------------------------------------------------------------

set design_name $project_name
create_bd_design $design_name
current_bd_design $design_name

# Create interface ports
set ddr4 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:ddr4_rtl:1.0 ddr4 ]

set cam_iic [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 cam_iic ]

set ddr4_refclk [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:diff_clock_rtl:1.0 ddr4_refclk ]
set_property -dict [ list \
CONFIG.FREQ_HZ {100040000} \
] $ddr4_refclk


# Create ports
set cam_slvs_p [ create_bd_port -dir I -from 3 -to 0 cam_slvs_p ]
set cam_slvs_n [ create_bd_port -dir I -from 3 -to 0 cam_slvs_n ]
set cam_slvsc_p [ create_bd_port -dir I cam_slvsc_p ]
set cam_slvsc_n [ create_bd_port -dir I cam_slvsc_n ]
set cam_extclk [ create_bd_port -dir O -type clk cam_extclk ]
set cam_reset_b [ create_bd_port -dir O cam_reset_b ]
set cam_saddr [ create_bd_port -dir O -from 0 -to 0 cam_saddr ]

# Create instance: frontpanel_0, and set properties
set frontpanel_0 [ create_bd_cell -type ip -vlnv opalkelly.com:ip:frontpanel frontpanel_0 ]
set_property -dict [list \
    CONFIG.BOARD {XEM8320-AU25P} \
    CONFIG.WI.ADDR_0 {0x00} \
    CONFIG.WI.ADDR_1 {0x1d} \
    CONFIG.WI.ADDR_2 {0x1e} \
    CONFIG.WI.ADDR_3 {0x1f} \
    CONFIG.WI.COUNT {4} \
    CONFIG.WO.ADDR_0 {0x3e} \
    CONFIG.WO.ADDR_1 {0x3f} \
    CONFIG.WO.COUNT {2} \
    CONFIG.TI.ADDR_0 {0x5f} \
    CONFIG.TI.COUNT {1} \
    CONFIG.BTPO.ADDR_0 {0xA0} \
    CONFIG.BTPO.COUNT {1} \
] $frontpanel_0

# Make host_interface external
make_bd_intf_pins_external [get_bd_intf_pins frontpanel_0/host_interface]

# Create instance: fp_to_axil_iwrap_0, and set properties
set block_name fp_to_axil_iwrap
set block_cell_name fp_to_axil_iwrap_0
if { [catch {set fp_to_axil_iwrap_0 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
   catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
   return 1
} elseif { $fp_to_axil_iwrap_0 eq "" } {
   catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
   return 1
}

set_property -dict [ list \
CONFIG.FREQ_HZ {100800000} \
] [get_bd_intf_pins /fp_to_axil_iwrap_0/m_axil]

# Create instance: axi_reset_iwrap_0, and set properties
set block_name axi_reset_iwrap
set block_cell_name axi_reset_iwrap_0
if { [catch {set axi_reset_iwrap_0 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
   catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
   return 1
} elseif { $axi_reset_iwrap_0 eq "" } {
   catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
   return 1
}

# Create instance: clk_wiz_0, and set properties
set clk_wiz_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:clk_wiz clk_wiz_0 ]
set_property -dict [list \
CONFIG.CLKOUT1_REQUESTED_OUT_FREQ {27} \
CONFIG.CLKOUT2_REQUESTED_OUT_FREQ {316.40625} \
CONFIG.CLKOUT2_USED {true} \
CONFIG.PRIM_IN_FREQ {100.8} \
CONFIG.PRIM_SOURCE {No_buffer} \
CONFIG.USE_LOCKED {false} \
CONFIG.USE_RESET {false} \
] $clk_wiz_0


# Create instance: axi_vdma_0, and set properties
set axi_vdma_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_vdma axi_vdma_0 ]
set_property -dict [list \
CONFIG.c_enable_s2mm_sts_reg {1} \
CONFIG.c_m_axis_mm2s_tdata_width {96} \
] $axi_vdma_0


# Create instance: ddr4_0, and set properties
set ddr4_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:ddr4 ddr4_0 ]
set_property -dict [list \
CONFIG.C0.DDR4_InputClockPeriod {9996} \
CONFIG.C0.DDR4_TimePeriod {833} \
CONFIG.C0.DDR4_CasLatency {17} \
CONFIG.C0.DDR4_CasWriteLatency {12} \
CONFIG.C0.DDR4_MemoryPart {MT40A512M16LY-075} \
CONFIG.C0.DDR4_DataWidth {16} \
CONFIG.C0.BANK_GROUP_WIDTH {1} \
] $ddr4_0


# Create instance: smartconnect_0, and set properties
set smartconnect_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect smartconnect_0 ]
set_property CONFIG.NUM_CLKS {1} $smartconnect_0


# Create instance: smartconnect_1, and set properties
set smartconnect_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect smartconnect_1 ]
set_property -dict [list \
CONFIG.NUM_CLKS {1} \
CONFIG.NUM_MI {5} \
CONFIG.NUM_SI {1} \
] $smartconnect_1


# Create instance: axi_iic_0, and set properties
set axi_iic_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_iic axi_iic_0 ]

# Create instance: ISPPipeline_accel_0, and set properties
set ISPPipeline_accel_0 [ create_bd_cell -type ip -vlnv xilinx.com:hls:ISPPipeline_accel ISPPipeline_accel_0 ]

# Create instance: smartconnect_2, and set properties
set smartconnect_2 [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect smartconnect_2 ]
set_property -dict [list \
CONFIG.NUM_CLKS {2} \
CONFIG.NUM_MI {2} \
CONFIG.NUM_SI {1} \
] $smartconnect_2


# Create instance: axis_switch_0, and set properties
set axis_switch_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_switch axis_switch_0 ]
set_property CONFIG.ROUTING_MODE {1} $axis_switch_0


# Create instance: axis_dwidth_converter_0, and set properties
set axis_dwidth_converter_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_dwidth_converter axis_dwidth_converter_0 ]
set_property CONFIG.M_TDATA_NUM_BYTES {4} $axis_dwidth_converter_0


# Create instance: v_tpg_0, and set properties
set v_tpg_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:v_tpg v_tpg_0 ]
set_property -dict [list \
CONFIG.COLOR_BAR {0} \
CONFIG.COLOR_SWEEP {0} \
CONFIG.DISPLAY_PORT {1} \
CONFIG.FOREGROUND {0} \
CONFIG.HAS_AXI4S_SLAVE {1} \
CONFIG.HAS_AXI4_YUV422_YUV420 {0} \
CONFIG.MAX_COLS {2304} \
CONFIG.MAX_ROWS {1296} \
CONFIG.SAMPLES_PER_CLOCK {4} \
CONFIG.ZONE_PLATE {0} \
] $v_tpg_0


# Create instance: histogram_accel_0, and set properties
set histogram_accel_0 [ create_bd_cell -type ip -vlnv xilinx.com:hls:histogram_accel histogram_accel_0 ]

# Create instance: proc_sys_reset_0, and set properties
set proc_sys_reset_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset proc_sys_reset_0 ]
set_property -dict [list \
CONFIG.C_AUX_RST_WIDTH {1} \
CONFIG.C_EXT_RST_WIDTH {1} \
] $proc_sys_reset_0


# Create instance: tie_high, and set properties
set tie_high [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant tie_high ]

# Create instance: util_vector_logic_0, and set properties
set util_vector_logic_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:util_vector_logic util_vector_logic_0 ]
set_property -dict [list \
CONFIG.C_OPERATION {not} \
CONFIG.C_SIZE {1} \
] $util_vector_logic_0


# Create instance: util_vector_logic_1, and set properties
set util_vector_logic_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:util_vector_logic util_vector_logic_1 ]
set_property -dict [list \
CONFIG.C_OPERATION {not} \
CONFIG.C_SIZE {1} \
] $util_vector_logic_1


# Create instance: util_vector_logic_2, and set properties
set util_vector_logic_2 [ create_bd_cell -type ip -vlnv xilinx.com:ip:util_vector_logic util_vector_logic_2 ]
set_property CONFIG.C_OPERATION {not} $util_vector_logic_2


# Create instance: proc_sys_reset_1, and set properties
set proc_sys_reset_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset proc_sys_reset_1 ]

# Create instance: bgr_to_rgb, and set properties
set bgr_to_rgb [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_subset_converter bgr_to_rgb ]
set_property -dict [list \
CONFIG.M_HAS_TKEEP {1} \
CONFIG.M_HAS_TLAST {1} \
CONFIG.M_HAS_TREADY {1} \
CONFIG.M_HAS_TSTRB {1} \
CONFIG.M_TDATA_NUM_BYTES {12} \
CONFIG.M_TDEST_WIDTH {1} \
CONFIG.M_TID_WIDTH {1} \
CONFIG.M_TUSER_WIDTH {1} \
CONFIG.S_HAS_TKEEP {1} \
CONFIG.S_HAS_TLAST {1} \
CONFIG.S_HAS_TREADY {1} \
CONFIG.S_HAS_TSTRB {1} \
CONFIG.S_TDATA_NUM_BYTES {12} \
CONFIG.S_TDEST_WIDTH {1} \
CONFIG.S_TID_WIDTH {1} \
CONFIG.S_TUSER_WIDTH {1} \
CONFIG.TDATA_REMAP {tdata[95:88],tdata[79:72],tdata[87:80],tdata[71:64],tdata[55:48],tdata[63:56],tdata[47:40],tdata[31:24],tdata[39:32],tdata[23:16],tdata[7:0],tdata[15:8]} \
] $bgr_to_rgb


# Create instance: v_vid_in_axi4s_0, and set properties
set v_vid_in_axi4s_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:v_vid_in_axi4s v_vid_in_axi4s_0 ]
set_property -dict [list \
CONFIG.C_M_AXIS_VIDEO_FORMAT {12} \
CONFIG.C_NATIVE_COMPONENT_WIDTH {10} \
CONFIG.C_PIXELS_PER_CLOCK {4} \
] $v_vid_in_axi4s_0


# Create instance: syzygy_camera_phy_0, and set properties
set block_name syzygy_camera_phy
set block_cell_name syzygy_camera_phy_0
if { [catch {set syzygy_camera_phy_0 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
 catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
 return 1
} elseif { $syzygy_camera_phy_0 eq "" } {
 catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
 return 1
}

# Create instance: axis_to_btpipeout_shim_0, and set properties
set block_name axis_to_btpipeout_shim
set block_cell_name axis_to_btpipeout_shim_0
if { [catch {set axis_to_btpipeout_shim_0 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
   catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
   return 1
} elseif { $axis_to_btpipeout_shim_0 eq "" } {
   catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
   return 1
}

# Create interface connections
connect_bd_intf_net -intf_net C0_SYS_CLK_0_1 [get_bd_intf_ports ddr4_refclk] [get_bd_intf_pins ddr4_0/C0_SYS_CLK]
connect_bd_intf_net -intf_net ISPPipeline_accel_0_m_axis_video [get_bd_intf_pins ISPPipeline_accel_0/m_axis_video] [get_bd_intf_pins bgr_to_rgb/S_AXIS]
connect_bd_intf_net -intf_net axi_iic_0_IIC [get_bd_intf_ports cam_iic] [get_bd_intf_pins axi_iic_0/IIC]
connect_bd_intf_net -intf_net axi_vdma_0_M_AXIS_MM2S [get_bd_intf_pins axi_vdma_0/M_AXIS_MM2S] [get_bd_intf_pins histogram_accel_0/s_axis_video]
connect_bd_intf_net -intf_net axi_vdma_0_M_AXI_MM2S [get_bd_intf_pins axi_vdma_0/M_AXI_MM2S] [get_bd_intf_pins smartconnect_0/S00_AXI]
connect_bd_intf_net -intf_net axi_vdma_0_M_AXI_S2MM [get_bd_intf_pins axi_vdma_0/M_AXI_S2MM] [get_bd_intf_pins smartconnect_0/S01_AXI]
connect_bd_intf_net -intf_net axis_subset_converter_0_M_AXIS [get_bd_intf_pins bgr_to_rgb/M_AXIS] [get_bd_intf_pins v_tpg_0/s_axis_video]
connect_bd_intf_net -intf_net axis_switch_0_M00_AXIS1 [get_bd_intf_pins axis_dwidth_converter_0/S_AXIS] [get_bd_intf_pins axis_switch_0/M00_AXIS]
connect_bd_intf_net [get_bd_intf_pins axis_dwidth_converter_0/M_AXIS] [get_bd_intf_pins axis_to_btpipeout_shim_0/s_axis_video]
connect_bd_intf_net [get_bd_intf_pins axis_to_btpipeout_shim_0/btpipeout_video] [get_bd_intf_pins frontpanel_0/btpipeouta0]
connect_bd_intf_net -intf_net ddr4_0_C0_DDR4 [get_bd_intf_ports ddr4] [get_bd_intf_pins ddr4_0/C0_DDR4]
connect_bd_intf_net -intf_net histogram_accel_0_m_axis_hist [get_bd_intf_pins histogram_accel_0/m_axis_hist] [get_bd_intf_pins axis_switch_0/S01_AXIS]
connect_bd_intf_net -intf_net histogram_accel_0_m_axis_video [get_bd_intf_pins histogram_accel_0/m_axis_video] [get_bd_intf_pins axis_switch_0/S00_AXIS]
connect_bd_intf_net -intf_net fp_to_axil_iwrap_0_m_axil [get_bd_intf_pins fp_to_axil_iwrap_0/m_axil] [get_bd_intf_pins smartconnect_1/S00_AXI]
connect_bd_intf_net -intf_net frontpanel_0_wirein00 [get_bd_intf_pins axi_reset_iwrap_0/wirein00_axi_reset] [get_bd_intf_pins frontpanel_0/wirein00]
connect_bd_intf_net -intf_net frontpanel_0_wirein1d [get_bd_intf_pins frontpanel_0/wirein1d] [get_bd_intf_pins fp_to_axil_iwrap_0/wirein1d_fp_to_axil_addr]
connect_bd_intf_net -intf_net frontpanel_0_wirein1e [get_bd_intf_pins frontpanel_0/wirein1e] [get_bd_intf_pins fp_to_axil_iwrap_0/wirein1e_fp_to_axil_data]
connect_bd_intf_net -intf_net frontpanel_0_wirein1f [get_bd_intf_pins frontpanel_0/wirein1f] [get_bd_intf_pins fp_to_axil_iwrap_0/wirein1f_fp_to_axil_timeout]
connect_bd_intf_net -intf_net frontpanel_0_wireout3e [get_bd_intf_pins frontpanel_0/wireout3e] [get_bd_intf_pins fp_to_axil_iwrap_0/wireout3e_fp_to_axil_data]
connect_bd_intf_net -intf_net frontpanel_0_wireout3f [get_bd_intf_pins frontpanel_0/wireout3f] [get_bd_intf_pins fp_to_axil_iwrap_0/wireout3f_fp_to_axil_status]
connect_bd_intf_net -intf_net frontpanel_0_triggerin5f [get_bd_intf_pins frontpanel_0/triggerin5f] [get_bd_intf_pins fp_to_axil_iwrap_0/triggerin5f_fp_to_axil_operation]
connect_bd_intf_net -intf_net smartconnect_0_M00_AXI [get_bd_intf_pins smartconnect_0/M00_AXI] [get_bd_intf_pins ddr4_0/C0_DDR4_S_AXI]
connect_bd_intf_net -intf_net smartconnect_1_M00_AXI [get_bd_intf_pins smartconnect_1/M00_AXI] [get_bd_intf_pins axi_vdma_0/S_AXI_LITE]
connect_bd_intf_net -intf_net smartconnect_1_M01_AXI [get_bd_intf_pins smartconnect_1/M01_AXI] [get_bd_intf_pins axi_iic_0/S_AXI]
connect_bd_intf_net -intf_net smartconnect_1_M02_AXI [get_bd_intf_pins smartconnect_1/M02_AXI] [get_bd_intf_pins smartconnect_2/S00_AXI]
connect_bd_intf_net -intf_net smartconnect_1_M03_AXI [get_bd_intf_pins histogram_accel_0/s_axi_control] [get_bd_intf_pins smartconnect_1/M03_AXI]
connect_bd_intf_net -intf_net smartconnect_1_M04_AXI [get_bd_intf_pins smartconnect_1/M04_AXI] [get_bd_intf_pins axis_switch_0/S_AXI_CTRL]
connect_bd_intf_net -intf_net smartconnect_2_M00_AXI [get_bd_intf_pins smartconnect_2/M00_AXI] [get_bd_intf_pins ISPPipeline_accel_0/s_axi_control]
connect_bd_intf_net -intf_net smartconnect_2_M01_AXI [get_bd_intf_pins smartconnect_2/M01_AXI] [get_bd_intf_pins v_tpg_0/s_axi_CTRL]
connect_bd_intf_net -intf_net syzygy_camera_phy_0_vid_io_in [get_bd_intf_pins syzygy_camera_phy_0/vid_io_in] [get_bd_intf_pins v_vid_in_axi4s_0/vid_io_in]
connect_bd_intf_net -intf_net szg_cam_if_0_axis_video [get_bd_intf_pins axi_vdma_0/S_AXIS_S2MM] [get_bd_intf_pins v_tpg_0/m_axis_video]
connect_bd_intf_net -intf_net v_vid_in_axi4s_0_video_out [get_bd_intf_pins v_vid_in_axi4s_0/video_out] [get_bd_intf_pins ISPPipeline_accel_0/s_axis_video]

# Create port connections
connect_bd_net -net axi_iic_0_gpo  [get_bd_pins axi_iic_0/gpo] \
[get_bd_ports cam_saddr]
connect_bd_net -net clk_wiz_0_clk_out1  [get_bd_pins syzygy_camera_phy_0/vid_clk] \
[get_bd_pins axi_vdma_0/s_axis_s2mm_aclk] \
[get_bd_pins smartconnect_2/aclk1] \
[get_bd_pins v_tpg_0/ap_clk] \
[get_bd_pins proc_sys_reset_0/slowest_sync_clk] \
[get_bd_pins proc_sys_reset_1/slowest_sync_clk] \
[get_bd_pins bgr_to_rgb/aclk] \
[get_bd_pins v_vid_in_axi4s_0/aclk] \
[get_bd_pins ISPPipeline_accel_0/ap_clk]
connect_bd_net -net clk_wiz_0_clk_out2  [get_bd_pins clk_wiz_0/clk_out2] \
[get_bd_pins syzygy_camera_phy_0/idelay_refclk]
connect_bd_net -net clk_wiz_0_clk_out3  [get_bd_pins clk_wiz_0/clk_out1] \
[get_bd_ports cam_extclk]
connect_bd_net -net ddr4_0_c0_ddr4_ui_clk  [get_bd_pins ddr4_0/c0_ddr4_ui_clk] \
[get_bd_pins axi_vdma_0/m_axi_s2mm_aclk] \
[get_bd_pins axi_vdma_0/m_axi_mm2s_aclk] \
[get_bd_pins smartconnect_0/aclk]
connect_bd_net -net ddr4_0_c0_ddr4_ui_clk_sync_rst  [get_bd_pins util_vector_logic_0/Res] \
[get_bd_pins smartconnect_0/aresetn] \
[get_bd_pins ddr4_0/c0_ddr4_aresetn]
connect_bd_net -net ddr4_0_c0_ddr4_ui_clk_sync_rst1  [get_bd_pins ddr4_0/c0_ddr4_ui_clk_sync_rst] \
[get_bd_pins util_vector_logic_0/Op1]
connect_bd_net -net frontpanel_0_okClk [get_bd_pins frontpanel_0/okClk] \
[get_bd_pins fp_to_axil_iwrap_0/aclk] \
[get_bd_pins axi_reset_iwrap_0/sync_clk] \
[get_bd_pins axi_vdma_0/s_axi_lite_aclk] \
[get_bd_pins smartconnect_1/aclk] \
[get_bd_pins axi_vdma_0/m_axis_mm2s_aclk] \
[get_bd_pins axi_iic_0/s_axi_aclk] \
[get_bd_pins smartconnect_2/aclk] \
[get_bd_pins axis_switch_0/aclk] \
[get_bd_pins axis_switch_0/s_axi_ctrl_aclk] \
[get_bd_pins axis_dwidth_converter_0/aclk] \
[get_bd_pins clk_wiz_0/clk_in1] \
[get_bd_pins histogram_accel_0/ap_clk] \
[get_bd_pins axis_to_btpipeout_shim_0/aclk]
connect_bd_net -net axi_reset_iwrap_0_axi_aresetn [get_bd_pins axi_reset_iwrap_0/axi_aresetn] \
[get_bd_pins util_vector_logic_1/Op1]
connect_bd_net -net axi_reset_iwrap_0_axil_aresetn [get_bd_pins axi_reset_iwrap_0/axil_aresetn] \
[get_bd_pins smartconnect_1/aresetn] \
[get_bd_pins axi_iic_0/s_axi_aresetn] \
[get_bd_pins proc_sys_reset_1/ext_reset_in] \
[get_bd_pins fp_to_axil_iwrap_0/aresetn]
connect_bd_net -net axi_reset_iwrap_0_axis_aresetn [get_bd_pins axi_reset_iwrap_0/axis_aresetn] \
[get_bd_pins proc_sys_reset_0/ext_reset_in] \
[get_bd_pins axis_dwidth_converter_0/aresetn] \
[get_bd_pins axis_switch_0/aresetn] \
[get_bd_pins util_vector_logic_2/Op1] \
[get_bd_pins axis_switch_0/s_axi_ctrl_aresetn] \
[get_bd_pins histogram_accel_0/ap_rst_n] \
[get_bd_pins axis_to_btpipeout_shim_0/aresetn]
connect_bd_net -net proc_sys_reset_0_peripheral_aresetn  [get_bd_pins proc_sys_reset_0/peripheral_aresetn] \
[get_bd_pins v_tpg_0/ap_rst_n] \
[get_bd_pins bgr_to_rgb/aresetn] \
[get_bd_pins v_vid_in_axi4s_0/aresetn] \
[get_bd_pins ISPPipeline_accel_0/ap_rst_n]
connect_bd_net -net proc_sys_reset_1_interconnect_aresetn  [get_bd_pins proc_sys_reset_1/interconnect_aresetn] \
[get_bd_pins smartconnect_2/aresetn]
connect_bd_net -net slvs_n_0_1  [get_bd_ports cam_slvs_n] \
[get_bd_pins syzygy_camera_phy_0/slvs_n]
connect_bd_net -net slvs_p_0_1  [get_bd_ports cam_slvs_p] \
[get_bd_pins syzygy_camera_phy_0/slvs_p]
connect_bd_net -net slvsc_n_0_1  [get_bd_ports cam_slvsc_n] \
[get_bd_pins syzygy_camera_phy_0/slvsc_n]
connect_bd_net -net slvsc_p_0_1  [get_bd_ports cam_slvsc_p] \
[get_bd_pins syzygy_camera_phy_0/slvsc_p]
connect_bd_net -net syzygy_camera_phy_0_cam_reset_b  [get_bd_pins syzygy_camera_phy_0/cam_reset_b] \
[get_bd_ports cam_reset_b]
connect_bd_net -net tie_high_dout  [get_bd_pins tie_high/dout] \
[get_bd_pins proc_sys_reset_0/dcm_locked] \
[get_bd_pins proc_sys_reset_1/dcm_locked] \
[get_bd_pins axi_vdma_0/axi_resetn] \
[get_bd_pins v_vid_in_axi4s_0/axis_enable] \
[get_bd_pins v_vid_in_axi4s_0/vid_io_in_ce] \
[get_bd_pins v_vid_in_axi4s_0/aclken]
connect_bd_net -net util_vector_logic_1_Res  [get_bd_pins util_vector_logic_1/Res] \
[get_bd_pins ddr4_0/sys_rst]
connect_bd_net -net util_vector_logic_2_Res  [get_bd_pins util_vector_logic_2/Res] \
[get_bd_pins syzygy_camera_phy_0/reset_async]

# Create address segments
assign_bd_address -offset 0x4CE00000 -range 0x00010000 -target_address_space [get_bd_addr_spaces fp_to_axil_iwrap_0/m_axil] [get_bd_addr_segs ISPPipeline_accel_0/s_axi_control/Reg] -force
assign_bd_address -offset 0x40800000 -range 0x00010000 -target_address_space [get_bd_addr_spaces fp_to_axil_iwrap_0/m_axil] [get_bd_addr_segs axi_iic_0/S_AXI/Reg] -force
assign_bd_address -offset 0x44A00000 -range 0x00010000 -target_address_space [get_bd_addr_spaces fp_to_axil_iwrap_0/m_axil] [get_bd_addr_segs axi_vdma_0/S_AXI_LITE/Reg] -force
assign_bd_address -offset 0x55200000 -range 0x00010000 -target_address_space [get_bd_addr_spaces fp_to_axil_iwrap_0/m_axil] [get_bd_addr_segs axis_switch_0/S_AXI_CTRL/Reg] -force
assign_bd_address -offset 0x51000000 -range 0x00010000 -target_address_space [get_bd_addr_spaces fp_to_axil_iwrap_0/m_axil] [get_bd_addr_segs histogram_accel_0/s_axi_control/Reg] -force
assign_bd_address -offset 0x59400000 -range 0x00010000 -target_address_space [get_bd_addr_spaces fp_to_axil_iwrap_0/m_axil] [get_bd_addr_segs v_tpg_0/s_axi_CTRL/Reg] -force
assign_bd_address -offset 0x80000000 -range 0x40000000 -target_address_space [get_bd_addr_spaces axi_vdma_0/Data_MM2S] [get_bd_addr_segs ddr4_0/C0_DDR4_MEMORY_MAP/C0_DDR4_ADDRESS_BLOCK] -force
assign_bd_address -offset 0x80000000 -range 0x40000000 -target_address_space [get_bd_addr_spaces axi_vdma_0/Data_S2MM] [get_bd_addr_segs ddr4_0/C0_DDR4_MEMORY_MAP/C0_DDR4_ADDRESS_BLOCK] -force

# Wrapper and Top
regenerate_bd_layout
set wrapperfile [make_wrapper -files [get_files ${project_name}.bd] -top -import]
set_property top ${project_name}_wrapper [current_fileset]
update_compile_order -fileset sources_1
