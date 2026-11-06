#----------------------------------------------------------------------
# project.tcl
#
# Project creation script.
#
# Copyright (c) 2025 Opal Kelly Incorporated
#----------------------------------------------------------------------

#--------------------------------------------------------------------------
# Check FrontPanel folder
#--------------------------------------------------------------------------

set fp_dir "./FrontPanel/BWMode3"

if {![file exists $fp_dir]} {
    puts "ERROR: 'FrontPanel/BWMode3' directory not found in current project directory."
    puts "Please ensure the following structure exists:"
    puts "    ./FrontPanel/BWMode3/okHost.vp"
    puts "    ./FrontPanel/BWMode3/okHost.xdc"
    puts ""
    puts "You can find these files for your product in the FrontPanel SDK"
    puts "under the 'FrontPanelHDL' directory."
    error "Missing FrontPanel HDL files for Bandwidth Mode 3."
}

#----------------------------------------------------------------------
# Project Creation
#----------------------------------------------------------------------

set project_name "Camera"
set project_dir  "Vivado_BWMode3_AU10P"

start_gui
create_project $project_name $project_dir -part xcau10p-ffvb676-1-e

# Add HDL sources
add_files -norecurse { \
    okHost_if.v \
}

# Add constraints
add_files -fileset constrs_1 -norecurse szg-hub1450.xdc

#----------------------------------------------------------------------
# Set IP Repositories
#----------------------------------------------------------------------
set ip_paths {}
lappend ip_paths \
[list ./HLS]

set_property ip_repo_paths $ip_paths [current_project]
update_ip_catalog

#----------------------------------------------------------------------
# Import FrontPanel HDL
#----------------------------------------------------------------------

puts "INFO: Importing FrontPanel sources for Bandwidth Mode 3 (10G Up / 2.5G Down)..."
add_files -norecurse [list "$fp_dir/okHost.vp"]
add_files -fileset constrs_1 -norecurse [list "$fp_dir/okHost.xdc"]

#----------------------------------------------------------------------
# Create Block Design
#----------------------------------------------------------------------

set design_name $project_name
create_bd_design $design_name
current_bd_design $design_name

# Create interface ports
set ddr4 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:ddr4_rtl:1.0 ddr4 ]

set pcam_iic [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 pcam_iic ]

set mipi [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:mipi_phy_rtl:1.0 mipi ]

set ddr4_refclk [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:diff_clock_rtl:1.0 ddr4_refclk ]
set_property -dict [ list \
CONFIG.FREQ_HZ {156764000} \
] $ddr4_refclk


# Create ports
set okUH [ create_bd_port -dir I -from 21 -to 0 okUH ]
set okHU [ create_bd_port -dir O -from 20 -to 0 okHU ]
set okUHU [ create_bd_port -dir IO -from 5 -to 0 okUHU ]
set pcam_power_en [ create_bd_port -dir O -from 0 -to 0 pcam_power_en ]

# Create instance: okHost_if, and set properties
set block_name okHost_if
set block_cell_name okHost_if
if { [catch {set okHost_if [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
 catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
 return 1
} elseif { $okHost_if eq "" } {
 catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
 return 1
}
set_property -dict [list \
CONFIG.CLK_FREQUENCY_HZ {156250000} \
CONFIG.MODE {3} \
CONFIG.M_AXIS_TDATA_WIDTH {32} \
CONFIG.M_AXI_DATA_WIDTH {64} \
CONFIG.OKHU_WIDTH_BITS {21} \
CONFIG.OKUHU_WIDTH_BITS {6} \
CONFIG.OKUH_WIDTH_BITS {22} \
CONFIG.S_AXIS_TDATA_WIDTH {64} \
] $okHost_if


set_property -dict [ list \
CONFIG.FREQ_HZ {156250000} \
] [get_bd_intf_pins /okHost_if/m_axi]

set_property -dict [ list \
CONFIG.FREQ_HZ {156250000} \
] [get_bd_intf_pins /okHost_if/m_axil]

set_property -dict [ list \
CONFIG.FREQ_HZ {156250000} \
] [get_bd_intf_pins /okHost_if/m_axis]

set_property -dict [ list \
CONFIG.FREQ_HZ {156250000} \
] [get_bd_intf_pins /okHost_if/s_axis]

set_property -dict [ list \
CONFIG.FREQ_HZ {156250000} \
] [get_bd_pins /okHost_if/aclk]

# Create instance: clk_wiz_0, and set properties
set clk_wiz_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:clk_wiz clk_wiz_0 ]
set_property -dict [list \
CONFIG.CLKIN1_JITTER_PS {64.0} \
CONFIG.CLKOUT1_JITTER {172.451} \
CONFIG.CLKOUT1_PHASE_ERROR {204.239} \
CONFIG.CLKOUT1_REQUESTED_OUT_FREQ {200} \
CONFIG.CLKOUT2_JITTER {194.337} \
CONFIG.CLKOUT2_PHASE_ERROR {204.239} \
CONFIG.CLKOUT2_REQUESTED_OUT_FREQ {100} \
CONFIG.CLKOUT2_USED {true} \
CONFIG.MMCM_CLKFBOUT_MULT_F {32.000} \
CONFIG.MMCM_CLKIN1_PERIOD {6.400} \
CONFIG.MMCM_CLKIN2_PERIOD {10.0} \
CONFIG.MMCM_CLKOUT0_DIVIDE_F {5.000} \
CONFIG.MMCM_CLKOUT1_DIVIDE {10} \
CONFIG.MMCM_DIVCLK_DIVIDE {5} \
CONFIG.NUM_OUT_CLKS {2} \
CONFIG.PRIM_IN_FREQ {156.25} \
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
CONFIG.C0.DDR4_CasLatency {15} \
CONFIG.C0.DDR4_CasWriteLatency {11} \
CONFIG.C0.DDR4_DataWidth {16} \
CONFIG.C0.DDR4_InputClockPeriod {6379} \
CONFIG.C0.DDR4_MemoryPart {MT40A512M16LY-075} \
CONFIG.C0.DDR4_TimePeriod {938} \
] $ddr4_0


# Create instance: smartconnect_0, and set properties
set smartconnect_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect smartconnect_0 ]
set_property CONFIG.NUM_CLKS {1} $smartconnect_0


# Create instance: smartconnect_1, and set properties
set smartconnect_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect smartconnect_1 ]
set_property -dict [list \
CONFIG.NUM_CLKS {1} \
CONFIG.NUM_MI {6} \
CONFIG.NUM_SI {1} \
] $smartconnect_1


# Create instance: axi_iic_0, and set properties
set axi_iic_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_iic axi_iic_0 ]
set_property CONFIG.C_DEFAULT_VALUE {0x01} $axi_iic_0


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
set_property CONFIG.M_TDATA_NUM_BYTES {8} $axis_dwidth_converter_0


# Create instance: v_tpg_0, and set properties
set v_tpg_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:v_tpg v_tpg_0 ]
set_property -dict [list \
CONFIG.COLOR_BAR {0} \
CONFIG.COLOR_SWEEP {0} \
CONFIG.DISPLAY_PORT {1} \
CONFIG.FOREGROUND {0} \
CONFIG.HAS_AXI4S_SLAVE {1} \
CONFIG.HAS_AXI4_YUV422_YUV420 {0} \
CONFIG.MAX_COLS {1920} \
CONFIG.MAX_ROWS {1080} \
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


# Create instance: proc_sys_reset_1, and set properties
set proc_sys_reset_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset proc_sys_reset_1 ]

# Create instance: mipi_csi2_rx_subsyst_0, and set properties
set mipi_csi2_rx_subsyst_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:mipi_csi2_rx_subsystem mipi_csi2_rx_subsyst_0 ]
set_property -dict [list \
CONFIG.CLK_LANE_IO_LOC {AE25} \
CONFIG.CMN_NUM_LANES {2} \
CONFIG.CMN_NUM_PIXELS {4} \
CONFIG.CMN_PXL_FORMAT {RAW10} \
CONFIG.CSI_CONTROLLER_REG_IF {true} \
CONFIG.C_DPHY_LANES {2} \
CONFIG.C_EXDES_BOARD {SP701} \
CONFIG.C_HS_LINE_RATE {420} \
CONFIG.C_HS_SETTLE_NS {158} \
CONFIG.DATA_LANE0_IO_LOC {AB25} \
CONFIG.DATA_LANE1_IO_LOC {AF24} \
CONFIG.DPY_EN_REG_IF {true} \
CONFIG.DPY_LINE_RATE {420} \
CONFIG.HP_IO_BANK_SELECTION {64} \
CONFIG.SupportLevel {1} \
CONFIG.VFB_TU_WIDTH {2} \
] $mipi_csi2_rx_subsyst_0


# Create instance: axis_subset_converter_0, and set properties
set axis_subset_converter_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_subset_converter axis_subset_converter_0 ]
set_property -dict [list \
CONFIG.M_HAS_TKEEP {0} \
CONFIG.M_HAS_TLAST {1} \
CONFIG.M_HAS_TREADY {1} \
CONFIG.M_HAS_TSTRB {0} \
CONFIG.M_TDATA_NUM_BYTES {4} \
CONFIG.M_TDEST_WIDTH {10} \
CONFIG.M_TID_WIDTH {0} \
CONFIG.M_TUSER_WIDTH {2} \
CONFIG.S_HAS_TKEEP {0} \
CONFIG.S_HAS_TLAST {1} \
CONFIG.S_HAS_TREADY {1} \
CONFIG.S_HAS_TSTRB {0} \
CONFIG.S_TDATA_NUM_BYTES {5} \
CONFIG.S_TDEST_WIDTH {10} \
CONFIG.S_TID_WIDTH {0} \
CONFIG.S_TUSER_WIDTH {2} \
CONFIG.TDATA_REMAP {tdata[39:32],tdata[29:22],tdata[19:12],tdata[9:2]} \
] $axis_subset_converter_0


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


# Create interface connections
connect_bd_intf_net -intf_net C0_SYS_CLK_0_1 [get_bd_intf_ports ddr4_refclk] [get_bd_intf_pins ddr4_0/C0_SYS_CLK]
connect_bd_intf_net -intf_net ISPPipeline_accel_0_m_axis_video [get_bd_intf_pins ISPPipeline_accel_0/m_axis_video] [get_bd_intf_pins bgr_to_rgb/S_AXIS]
connect_bd_intf_net -intf_net axi_iic_0_IIC [get_bd_intf_ports pcam_iic] [get_bd_intf_pins axi_iic_0/IIC]
connect_bd_intf_net -intf_net axi_vdma_0_M_AXIS_MM2S [get_bd_intf_pins axi_vdma_0/M_AXIS_MM2S] [get_bd_intf_pins histogram_accel_0/s_axis_video]
connect_bd_intf_net -intf_net axi_vdma_0_M_AXI_MM2S [get_bd_intf_pins axi_vdma_0/M_AXI_MM2S] [get_bd_intf_pins smartconnect_0/S00_AXI]
connect_bd_intf_net -intf_net axi_vdma_0_M_AXI_S2MM [get_bd_intf_pins axi_vdma_0/M_AXI_S2MM] [get_bd_intf_pins smartconnect_0/S01_AXI]
connect_bd_intf_net -intf_net axis_subset_converter_0_M_AXIS [get_bd_intf_pins axis_subset_converter_0/M_AXIS] [get_bd_intf_pins ISPPipeline_accel_0/s_axis_video]
connect_bd_intf_net -intf_net axis_switch_0_M00_AXIS [get_bd_intf_pins axis_dwidth_converter_0/M_AXIS] [get_bd_intf_pins okHost_if/s_axis]
connect_bd_intf_net -intf_net axis_switch_0_M00_AXIS1 [get_bd_intf_pins axis_dwidth_converter_0/S_AXIS] [get_bd_intf_pins axis_switch_0/M00_AXIS]
connect_bd_intf_net -intf_net bgr_to_rgb_M_AXIS [get_bd_intf_pins bgr_to_rgb/M_AXIS] [get_bd_intf_pins v_tpg_0/s_axis_video]
connect_bd_intf_net -intf_net ddr4_0_C0_DDR4 [get_bd_intf_ports ddr4] [get_bd_intf_pins ddr4_0/C0_DDR4]
connect_bd_intf_net -intf_net histogram_accel_0_m_axis_hist [get_bd_intf_pins histogram_accel_0/m_axis_hist] [get_bd_intf_pins axis_switch_0/S01_AXIS]
connect_bd_intf_net -intf_net histogram_accel_0_m_axis_video [get_bd_intf_pins histogram_accel_0/m_axis_video] [get_bd_intf_pins axis_switch_0/S00_AXIS]
connect_bd_intf_net -intf_net mipi_csi2_rx_subsyst_0_video_out [get_bd_intf_pins mipi_csi2_rx_subsyst_0/video_out] [get_bd_intf_pins axis_subset_converter_0/S_AXIS]
connect_bd_intf_net -intf_net mipi_phy_if_0_1 [get_bd_intf_ports mipi] [get_bd_intf_pins mipi_csi2_rx_subsyst_0/mipi_phy_if]
connect_bd_intf_net -intf_net okHost_if_m_axil [get_bd_intf_pins okHost_if/m_axil] [get_bd_intf_pins smartconnect_1/S00_AXI]
connect_bd_intf_net -intf_net smartconnect_0_M00_AXI [get_bd_intf_pins smartconnect_0/M00_AXI] [get_bd_intf_pins ddr4_0/C0_DDR4_S_AXI]
connect_bd_intf_net -intf_net smartconnect_1_M00_AXI [get_bd_intf_pins smartconnect_1/M00_AXI] [get_bd_intf_pins axi_vdma_0/S_AXI_LITE]
connect_bd_intf_net -intf_net smartconnect_1_M01_AXI [get_bd_intf_pins smartconnect_1/M01_AXI] [get_bd_intf_pins axi_iic_0/S_AXI]
connect_bd_intf_net -intf_net smartconnect_1_M02_AXI [get_bd_intf_pins smartconnect_1/M02_AXI] [get_bd_intf_pins smartconnect_2/S00_AXI]
connect_bd_intf_net -intf_net smartconnect_1_M03_AXI [get_bd_intf_pins histogram_accel_0/s_axi_control] [get_bd_intf_pins smartconnect_1/M03_AXI]
connect_bd_intf_net -intf_net smartconnect_1_M04_AXI [get_bd_intf_pins smartconnect_1/M04_AXI] [get_bd_intf_pins axis_switch_0/S_AXI_CTRL]
connect_bd_intf_net -intf_net smartconnect_1_M05_AXI [get_bd_intf_pins smartconnect_1/M05_AXI] [get_bd_intf_pins mipi_csi2_rx_subsyst_0/csirxss_s_axi]
connect_bd_intf_net -intf_net smartconnect_2_M00_AXI [get_bd_intf_pins smartconnect_2/M00_AXI] [get_bd_intf_pins ISPPipeline_accel_0/s_axi_control]
connect_bd_intf_net -intf_net smartconnect_2_M01_AXI [get_bd_intf_pins smartconnect_2/M01_AXI] [get_bd_intf_pins v_tpg_0/s_axi_CTRL]
connect_bd_intf_net -intf_net szg_cam_if_0_axis_video [get_bd_intf_pins axi_vdma_0/S_AXIS_S2MM] [get_bd_intf_pins v_tpg_0/m_axis_video]

# Create port connections
connect_bd_net -net Net  [get_bd_ports okUHU] \
[get_bd_pins okHost_if/okUHU]
connect_bd_net -net axi_iic_0_gpo  [get_bd_pins axi_iic_0/gpo] \
[get_bd_ports pcam_power_en]
connect_bd_net -net clk_wiz_0_clk_out2  [get_bd_pins clk_wiz_0/clk_out1] \
[get_bd_pins mipi_csi2_rx_subsyst_0/dphy_clk_200M]
connect_bd_net -net clk_wiz_0_clk_out3  [get_bd_pins clk_wiz_0/clk_out2] \
[get_bd_pins mipi_csi2_rx_subsyst_0/video_aclk] \
[get_bd_pins axi_vdma_0/s_axis_s2mm_aclk] \
[get_bd_pins smartconnect_2/aclk1] \
[get_bd_pins v_tpg_0/ap_clk] \
[get_bd_pins proc_sys_reset_0/slowest_sync_clk] \
[get_bd_pins axis_subset_converter_0/aclk] \
[get_bd_pins bgr_to_rgb/aclk] \
[get_bd_pins ISPPipeline_accel_0/ap_clk]
connect_bd_net -net ddr4_0_c0_ddr4_ui_clk  [get_bd_pins ddr4_0/c0_ddr4_ui_clk] \
[get_bd_pins axi_vdma_0/m_axi_s2mm_aclk] \
[get_bd_pins axi_vdma_0/m_axi_mm2s_aclk] \
[get_bd_pins smartconnect_0/aclk]
connect_bd_net -net ddr4_0_c0_ddr4_ui_clk_sync_rst  [get_bd_pins util_vector_logic_0/Res] \
[get_bd_pins smartconnect_0/aresetn] \
[get_bd_pins ddr4_0/c0_ddr4_aresetn]
connect_bd_net -net ddr4_0_c0_ddr4_ui_clk_sync_rst1  [get_bd_pins ddr4_0/c0_ddr4_ui_clk_sync_rst] \
[get_bd_pins util_vector_logic_0/Op1]
connect_bd_net -net okHost_if_aclk  [get_bd_pins okHost_if/aclk] \
[get_bd_pins axi_vdma_0/s_axi_lite_aclk] \
[get_bd_pins smartconnect_1/aclk] \
[get_bd_pins axi_vdma_0/m_axis_mm2s_aclk] \
[get_bd_pins axi_iic_0/s_axi_aclk] \
[get_bd_pins smartconnect_2/aclk] \
[get_bd_pins axis_switch_0/aclk] \
[get_bd_pins axis_switch_0/s_axi_ctrl_aclk] \
[get_bd_pins axis_dwidth_converter_0/aclk] \
[get_bd_pins histogram_accel_0/ap_clk] \
[get_bd_pins proc_sys_reset_1/slowest_sync_clk] \
[get_bd_pins clk_wiz_0/clk_in1] \
[get_bd_pins mipi_csi2_rx_subsyst_0/lite_aclk]
connect_bd_net -net okHost_if_axi_aresetn  [get_bd_pins okHost_if/axi_aresetn] \
[get_bd_pins util_vector_logic_1/Op1]
connect_bd_net -net okHost_if_axil_aresetn  [get_bd_pins okHost_if/axil_aresetn] \
[get_bd_pins smartconnect_1/aresetn] \
[get_bd_pins proc_sys_reset_1/ext_reset_in] \
[get_bd_pins axi_iic_0/s_axi_aresetn] \
[get_bd_pins mipi_csi2_rx_subsyst_0/lite_aresetn]
connect_bd_net -net okHost_if_axis_aresetn  [get_bd_pins okHost_if/axis_aresetn] \
[get_bd_pins proc_sys_reset_0/ext_reset_in] \
[get_bd_pins histogram_accel_0/ap_rst_n] \
[get_bd_pins axis_dwidth_converter_0/aresetn] \
[get_bd_pins axis_switch_0/aresetn] \
[get_bd_pins axis_switch_0/s_axi_ctrl_aresetn]
connect_bd_net -net okHost_if_okHU  [get_bd_pins okHost_if/okHU] \
[get_bd_ports okHU]
connect_bd_net -net okUH_0_1  [get_bd_ports okUH] \
[get_bd_pins okHost_if/okUH]
connect_bd_net -net proc_sys_reset_0_peripheral_aresetn  [get_bd_pins proc_sys_reset_0/peripheral_aresetn] \
[get_bd_pins v_tpg_0/ap_rst_n] \
[get_bd_pins mipi_csi2_rx_subsyst_0/video_aresetn] \
[get_bd_pins axis_subset_converter_0/aresetn] \
[get_bd_pins bgr_to_rgb/aresetn] \
[get_bd_pins ISPPipeline_accel_0/ap_rst_n]
connect_bd_net -net proc_sys_reset_1_interconnect_aresetn  [get_bd_pins proc_sys_reset_1/interconnect_aresetn] \
[get_bd_pins smartconnect_2/aresetn]
connect_bd_net -net tie_high_dout  [get_bd_pins tie_high/dout] \
[get_bd_pins proc_sys_reset_0/dcm_locked] \
[get_bd_pins proc_sys_reset_1/dcm_locked] \
[get_bd_pins axi_vdma_0/axi_resetn]
connect_bd_net -net util_vector_logic_1_Res  [get_bd_pins util_vector_logic_1/Res] \
[get_bd_pins ddr4_0/sys_rst]

# Create address segments
assign_bd_address -offset 0x4CE00000 -range 0x00010000 -target_address_space [get_bd_addr_spaces okHost_if/m_axil] [get_bd_addr_segs ISPPipeline_accel_0/s_axi_control/Reg] -force
assign_bd_address -offset 0x40800000 -range 0x00010000 -target_address_space [get_bd_addr_spaces okHost_if/m_axil] [get_bd_addr_segs axi_iic_0/S_AXI/Reg] -force
assign_bd_address -offset 0x44A00000 -range 0x00010000 -target_address_space [get_bd_addr_spaces okHost_if/m_axil] [get_bd_addr_segs axi_vdma_0/S_AXI_LITE/Reg] -force
assign_bd_address -offset 0x55200000 -range 0x00010000 -target_address_space [get_bd_addr_spaces okHost_if/m_axil] [get_bd_addr_segs axis_switch_0/S_AXI_CTRL/Reg] -force
assign_bd_address -offset 0x51000000 -range 0x00010000 -target_address_space [get_bd_addr_spaces okHost_if/m_axil] [get_bd_addr_segs histogram_accel_0/s_axi_control/Reg] -force
assign_bd_address -offset 0x5D600000 -range 0x00002000 -target_address_space [get_bd_addr_spaces okHost_if/m_axil] [get_bd_addr_segs mipi_csi2_rx_subsyst_0/csirxss_s_axi/Reg] -force
assign_bd_address -offset 0x59400000 -range 0x00010000 -target_address_space [get_bd_addr_spaces okHost_if/m_axil] [get_bd_addr_segs v_tpg_0/s_axi_CTRL/Reg] -force
assign_bd_address -offset 0x80000000 -range 0x40000000 -target_address_space [get_bd_addr_spaces axi_vdma_0/Data_MM2S] [get_bd_addr_segs ddr4_0/C0_DDR4_MEMORY_MAP/C0_DDR4_ADDRESS_BLOCK] -force
assign_bd_address -offset 0x80000000 -range 0x40000000 -target_address_space [get_bd_addr_spaces axi_vdma_0/Data_S2MM] [get_bd_addr_segs ddr4_0/C0_DDR4_MEMORY_MAP/C0_DDR4_ADDRESS_BLOCK] -force

# Wrapper and Top
regenerate_bd_layout
set wrapperfile [make_wrapper -files [get_files ${project_name}.bd] -top -import]
set_property top ${project_name}_wrapper [current_fileset]
update_compile_order -fileset sources_1
