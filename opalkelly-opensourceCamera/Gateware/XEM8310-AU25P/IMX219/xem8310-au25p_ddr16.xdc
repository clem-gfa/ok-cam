############################################################################
# XEM8310-AU25P - AMD constraints file (DDR4 16-bit variant, bank 67 free for the MIPI D-PHY)
# Derived from the XEM8320-AU25P constraints (Copyright (c) 2025 Opal Kelly Incorporated).
# Pin locations taken from XEM8310-AU25P-A03-Schematic.pdf and DARWIN.pdf (carrier board);
# the 71 DDR4 pins were cross-checked against the Opal Kelly xem8310.xdc template (no difference).
############################################################################

set_property CFGBVS GND [current_design]
set_property CONFIG_VOLTAGE 1.8 [current_design]
set_property BITSTREAM.GENERAL.COMPRESS True [current_design]

# DDR4 reference clock: DSC1123DI2-100.0000 (100 MHz LVDS), bank 66 (IO_L12P/N_T1U_N10/11_GC_66)
set_property PACKAGE_PIN J23 [get_ports {ddr4_refclk_clk_p}]
set_property IOSTANDARD LVDS [get_ports {ddr4_refclk_clk_p}]
# AC-coupled with an on-board bias network (XEM8310 schematic sheet 6): no internal termination.
set_property DIFF_TERM FALSE [get_ports {ddr4_refclk_clk_p}]
create_clock -period 10 -name refclk [get_ports ddr4_refclk_clk_p]

############################################################################
## DARWIN carrier board - J1 (Raspberry Pi camera, FFC 15 pins)
## I2C and GPIO go through level translators (PCA9306 / SN74AUP1T34) powered from
## +1.8V, so the FPGA side is 1.8 V: VIO2 MUST be set to 1.8 V on the XEM8310.
############################################################################

# MC1-64 (B84_L1N)  -> J1-13 SCL
set_property PACKAGE_PIN AF15 [get_ports {pcam_iic_scl_io}]
set_property IOSTANDARD LVCMOS18 [get_ports {pcam_iic_scl_io}]
set_property DRIVE 8 [get_ports {pcam_iic_scl_io}]
# MC1-66 (B84_L9P)  -> J1-14 SDA
set_property PACKAGE_PIN W16 [get_ports {pcam_iic_sda_io}]
set_property IOSTANDARD LVCMOS18 [get_ports {pcam_iic_sda_io}]
set_property DRIVE 8 [get_ports {pcam_iic_sda_io}]
# MC1-72 (B84_L3N)  -> J1-11 GPIO0 (CAM_GPIO = sensor enable / shutdown)
set_property PACKAGE_PIN AE15 [get_ports {pcam_power_en}]
set_property IOSTANDARD LVCMOS18 [get_ports {pcam_power_en}]

# MIPI D-PHY pins (bank 67, MC3) are placed by the MIPI CSI-2 RX subsystem IP
# (CLK_LANE_IO_LOC / DATA_LANEx_IO_LOC in project.tcl):
#   CLK   = IO_L16P/N_T2U_N6/N7_QBC_AD3P/N_67 = A17/A18  (MC3-66/68, J1-9/8)
#   DATA0 = IO_L17P/N_T2U_N8/N9_AD10P/N_67    = B15/A15  (MC3-62/64, J1-3/2)
#   DATA1 = IO_L18P/N_T2U_N10/N11_AD2P/N_67   = A19/A20  (MC3-65/67, J1-6/5)

# ----------------------- DDR4 (2 x MT40A512M16LY-075, 32 bits, banks 66/67) -----------------------
# Source: XEM8310-AU25P-A03 schematic, sheet 6 (HP Bank 66 / HP Bank 67)
set_property PACKAGE_PIN J20 [get_ports {ddr4_adr[0]}]
set_property PACKAGE_PIN M26 [get_ports {ddr4_adr[1]}]
set_property PACKAGE_PIN J19 [get_ports {ddr4_adr[2]}]
set_property PACKAGE_PIN L23 [get_ports {ddr4_adr[3]}]
set_property PACKAGE_PIN M25 [get_ports {ddr4_adr[4]}]
set_property PACKAGE_PIN L20 [get_ports {ddr4_adr[5]}]
set_property PACKAGE_PIN J21 [get_ports {ddr4_adr[6]}]
set_property PACKAGE_PIN M20 [get_ports {ddr4_adr[7]}]
set_property PACKAGE_PIN K20 [get_ports {ddr4_adr[8]}]
set_property PACKAGE_PIN M24 [get_ports {ddr4_adr[9]}]
set_property PACKAGE_PIN L25 [get_ports {ddr4_adr[10]}]
set_property PACKAGE_PIN K25 [get_ports {ddr4_adr[11]}]
set_property PACKAGE_PIN L22 [get_ports {ddr4_adr[12]}]
set_property PACKAGE_PIN M21 [get_ports {ddr4_adr[13]}]
set_property PACKAGE_PIN F22 [get_ports {ddr4_adr[14]}]
set_property PACKAGE_PIN K23 [get_ports {ddr4_adr[15]}]
set_property PACKAGE_PIN K22 [get_ports {ddr4_adr[16]}]

set_property PACKAGE_PIN K18 [get_ports {ddr4_ba[0]}]
set_property PACKAGE_PIN L18 [get_ports {ddr4_ba[1]}]
set_property PACKAGE_PIN G22 [get_ports {ddr4_bg[0]}]

set_property PACKAGE_PIN E25 [get_ports {ddr4_dm_n[0]}]
set_property PACKAGE_PIN G24 [get_ports {ddr4_dm_n[1]}]

set_property PACKAGE_PIN E23 [get_ports {ddr4_dq[0]}]
set_property PACKAGE_PIN D25 [get_ports {ddr4_dq[1]}]
set_property PACKAGE_PIN D24 [get_ports {ddr4_dq[2]}]
set_property PACKAGE_PIN C26 [get_ports {ddr4_dq[3]}]
set_property PACKAGE_PIN F23 [get_ports {ddr4_dq[4]}]
set_property PACKAGE_PIN B25 [get_ports {ddr4_dq[5]}]
set_property PACKAGE_PIN D26 [get_ports {ddr4_dq[6]}]
set_property PACKAGE_PIN B26 [get_ports {ddr4_dq[7]}]
set_property PACKAGE_PIN H26 [get_ports {ddr4_dq[8]}]
set_property PACKAGE_PIN H22 [get_ports {ddr4_dq[9]}]
set_property PACKAGE_PIN G26 [get_ports {ddr4_dq[10]}]
set_property PACKAGE_PIN H24 [get_ports {ddr4_dq[11]}]
set_property PACKAGE_PIN J26 [get_ports {ddr4_dq[12]}]
set_property PACKAGE_PIN H21 [get_ports {ddr4_dq[13]}]
set_property PACKAGE_PIN J25 [get_ports {ddr4_dq[14]}]
set_property PACKAGE_PIN H23 [get_ports {ddr4_dq[15]}]

set_property PACKAGE_PIN C24 [get_ports {ddr4_dqs_c[0]}]
set_property PACKAGE_PIN F25 [get_ports {ddr4_dqs_c[1]}]
set_property PACKAGE_PIN D23 [get_ports {ddr4_dqs_t[0]}]
set_property PACKAGE_PIN F24 [get_ports {ddr4_dqs_t[1]}]

set_property PACKAGE_PIN G25 [get_ports {ddr4_act_n}]
set_property PACKAGE_PIN M19 [get_ports {ddr4_ck_t[0]}]
set_property PACKAGE_PIN L19 [get_ports {ddr4_ck_c[0]}]
set_property PACKAGE_PIN K21 [get_ports {ddr4_cke[0]}]
set_property PACKAGE_PIN K26 [get_ports {ddr4_cs_n[0]}]
set_property PACKAGE_PIN E26 [get_ports {ddr4_odt[0]}]
set_property PACKAGE_PIN L24 [get_ports {ddr4_reset_n}]
