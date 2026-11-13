############################################################################
# XEM8310-AU25P - MIPI-only isolation test (no DDR4, no DDR4 reference clock)
# Derived from the XEM8320-AU25P constraints (Copyright (c) 2025 Opal Kelly Incorporated).
# Pin locations taken from XEM8310-AU25P-A03-Schematic.pdf and DARWIN.pdf (carrier board);
############################################################################

set_property CFGBVS GND [current_design]
set_property CONFIG_VOLTAGE 1.8 [current_design]
set_property BITSTREAM.GENERAL.COMPRESS True [current_design]

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

