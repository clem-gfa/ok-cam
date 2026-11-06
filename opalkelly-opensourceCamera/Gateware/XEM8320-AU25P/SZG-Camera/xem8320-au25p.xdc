############################################################################
# XEM8320-AU25P - AMD constraints file
#
# Copyright (c) 2025 Opal Kelly Incorporated
############################################################################

set_property CFGBVS GND [current_design]
set_property CONFIG_VOLTAGE 1.8 [current_design]
set_property BITSTREAM.GENERAL.COMPRESS True [current_design]


set_property PACKAGE_PIN AD20 [get_ports {ddr4_refclk_clk_p}]
set_property IOSTANDARD LVDS [get_ports {ddr4_refclk_clk_p}]
create_clock -period 10 -name refclk [get_ports ddr4_refclk_clk_p]

# reset_serdes_cnt_reg*
set_false_path -to [get_pins Camera_i/syzygy_camera_phy_0/inst/reset_serdes_cnt_reg*/CLR]
set_false_path -to [get_pins Camera_i/syzygy_camera_phy_0/inst/reset_serdes_cnt_reg*/PRE]

# reset_sync_reg
set_false_path -to [get_pins Camera_i/syzygy_camera_phy_0/inst/reset_sync_reg/PRE]

# reset_idelay_cnt_reg*
set_false_path -to [get_pins Camera_i/syzygy_camera_phy_0/inst/reset_idelay_cnt_reg*/CLR]
set_false_path -to [get_pins Camera_i/syzygy_camera_phy_0/inst/reset_idelay_cnt_reg*/PRE]

# reset_idelay_reg
set_false_path -to [get_pins Camera_i/syzygy_camera_phy_0/inst/reset_idelay_reg/PRE]


############################################################################
## SYZYGY PORTA - SZG-CAMERA Peripheral
############################################################################

# PORTA-5 (S0_D0P)
set_property PACKAGE_PIN L18 [get_ports {cam_slvs_p[0]}]

# PORTA-7 (S2_D0N)
set_property PACKAGE_PIN K18 [get_ports {cam_slvs_n[0]}]

# PORTA-8 (S3_D1N)
set_property PACKAGE_PIN M26 [get_ports {cam_reset_b}]
set_property IOSTANDARD LVCMOS18 [get_ports {cam_reset_b}]

# PORTA-9 (S4_D2P)
set_property PACKAGE_PIN M20 [get_ports {cam_slvs_p[1]}]

# PORTA-11 (S6_D2N)
set_property PACKAGE_PIN M21 [get_ports {cam_slvs_n[1]}]

# PORTA-13 (S8_D4P)
set_property PACKAGE_PIN J19 [get_ports {cam_slvs_p[2]}]
# PORTA-15 (S10_D4N)
set_property PACKAGE_PIN J20 [get_ports {cam_slvs_n[2]}]

# PORTA-17 (S12_D6P)
set_property PACKAGE_PIN L22 [get_ports {cam_slvs_p[3]}]

# PORTA-19 (S14_D6N)
set_property PACKAGE_PIN L23 [get_ports {cam_slvs_n[3]}]

# PORTA-21 (S16)
set_property PACKAGE_PIN H24 [get_ports {cam_iic_sda_io}]
set_property IOSTANDARD LVCMOS18 [get_ports {cam_iic_sda_io}]

# PORTA-23 (S18)
set_property PACKAGE_PIN J21 [get_ports {cam_iic_scl_io}]
set_property IOSTANDARD LVCMOS18 [get_ports {cam_iic_scl_io}]

# PORTA-25 (S20)
set_property PACKAGE_PIN H23 [get_ports {cam_saddr}]
set_property IOSTANDARD LVCMOS18 [get_ports {cam_saddr}]

# PORTA-33 (P2C_CLKP)
set_property PACKAGE_PIN J23 [get_ports {cam_slvsc_p}]
set_property IOSTANDARD LVDS [get_ports {cam_slvsc_p}]

# PORTA-34 (C2P_CLKP)
set_property PACKAGE_PIN H26 [get_ports {cam_extclk}]
set_property IOSTANDARD LVCMOS18 [get_ports {cam_extclk}]

# PORTA-35 (P2C_CLKN)
set_property PACKAGE_PIN J24 [get_ports {cam_slvsc_n}]
set_property IOSTANDARD LVDS [get_ports {cam_slvsc_n}]

set_property IOSTANDARD LVDS [get_ports {cam_slvs_p[*]}]
set_property IOSTANDARD LVDS [get_ports {cam_slvs_n[*]}]

create_clock -period 6.80 -name slvsc_p -waveform {0.000 3.401} [get_ports cam_slvsc_p]
set_input_delay -clock [get_clocks slvsc_p] -clock_fall -min -add_delay 1.020 [get_ports {cam_slvs_n[*]}]
set_input_delay -clock [get_clocks slvsc_p] -clock_fall -max -add_delay 2.381 [get_ports {cam_slvs_n[*]}]
set_input_delay -clock [get_clocks slvsc_p] -min -add_delay 1.020 [get_ports {cam_slvs_n[*]}]
set_input_delay -clock [get_clocks slvsc_p] -max -add_delay 2.381 [get_ports {cam_slvs_n[*]}]
set_input_delay -clock [get_clocks slvsc_p] -clock_fall -min -add_delay 1.020 [get_ports {cam_slvs_p[*]}]
set_input_delay -clock [get_clocks slvsc_p] -clock_fall -max -add_delay 2.381 [get_ports {cam_slvs_p[*]}]
set_input_delay -clock [get_clocks slvsc_p] -min -add_delay 1.020 [get_ports {cam_slvs_p[*]}]
set_input_delay -clock [get_clocks slvsc_p] -max -add_delay 2.381 [get_ports {cam_slvs_p[*]}]


# ----------------------- DDR -----------------------
set_property PACKAGE_PIN AD18 [get_ports {ddr4_adr[0]}]
set_property PACKAGE_PIN AE17 [get_ports {ddr4_adr[1]}]
set_property PACKAGE_PIN AB17 [get_ports {ddr4_adr[2]}]
set_property PACKAGE_PIN AE18 [get_ports {ddr4_adr[3]}]
set_property PACKAGE_PIN AD19 [get_ports {ddr4_adr[4]}]
set_property PACKAGE_PIN AF17 [get_ports {ddr4_adr[5]}]
set_property PACKAGE_PIN Y17 [get_ports {ddr4_adr[6]}]
set_property PACKAGE_PIN AE16 [get_ports {ddr4_adr[7]}]
set_property PACKAGE_PIN AA17 [get_ports {ddr4_adr[8]}]
set_property PACKAGE_PIN AC17 [get_ports {ddr4_adr[9]}]
set_property PACKAGE_PIN AC19 [get_ports {ddr4_adr[10]}]
set_property PACKAGE_PIN AC16 [get_ports {ddr4_adr[11]}]
set_property PACKAGE_PIN AF20 [get_ports {ddr4_adr[12]}]
set_property PACKAGE_PIN AD16 [get_ports {ddr4_adr[13]}]
set_property PACKAGE_PIN AA19 [get_ports {ddr4_adr[14]}]
set_property PACKAGE_PIN AF19 [get_ports {ddr4_adr[15]}]
set_property PACKAGE_PIN AA18 [get_ports {ddr4_adr[16]}]

set_property PACKAGE_PIN AC18 [get_ports {ddr4_ba[0]}]
set_property PACKAGE_PIN AF18 [get_ports {ddr4_ba[1]}]
set_property PACKAGE_PIN AB19 [get_ports {ddr4_bg[0]}]

set_property PACKAGE_PIN AE25 [get_ports {ddr4_dm_n[0]}]
set_property PACKAGE_PIN AE22 [get_ports {ddr4_dm_n[1]}]

set_property PACKAGE_PIN AF24 [get_ports {ddr4_dq[0]}]
set_property PACKAGE_PIN AB25 [get_ports {ddr4_dq[1]}]
set_property PACKAGE_PIN AB26 [get_ports {ddr4_dq[2]}]
set_property PACKAGE_PIN AC24 [get_ports {ddr4_dq[3]}]
set_property PACKAGE_PIN AF25 [get_ports {ddr4_dq[4]}]
set_property PACKAGE_PIN AB24 [get_ports {ddr4_dq[5]}]
set_property PACKAGE_PIN AD24 [get_ports {ddr4_dq[6]}]
set_property PACKAGE_PIN AD25 [get_ports {ddr4_dq[7]}]
set_property PACKAGE_PIN AB21 [get_ports {ddr4_dq[8]}]
set_property PACKAGE_PIN AE21 [get_ports {ddr4_dq[9]}]
set_property PACKAGE_PIN AE23 [get_ports {ddr4_dq[10]}]
set_property PACKAGE_PIN AD23 [get_ports {ddr4_dq[11]}]
set_property PACKAGE_PIN AC23 [get_ports {ddr4_dq[12]}]
set_property PACKAGE_PIN AD21 [get_ports {ddr4_dq[13]}]
set_property PACKAGE_PIN AC22 [get_ports {ddr4_dq[14]}]
set_property PACKAGE_PIN AC21 [get_ports {ddr4_dq[15]}]

set_property PACKAGE_PIN AD26 [get_ports {ddr4_dqs_c[0]}]
set_property PACKAGE_PIN AB22 [get_ports {ddr4_dqs_c[1]}]
set_property PACKAGE_PIN AC26 [get_ports {ddr4_dqs_t[0]}]
set_property PACKAGE_PIN AA22 [get_ports {ddr4_dqs_t[1]}]

set_property PACKAGE_PIN Y18 [get_ports {ddr4_act_n}]
set_property PACKAGE_PIN Y20 [get_ports {ddr4_ck_t[0]}]
set_property PACKAGE_PIN Y21 [get_ports {ddr4_ck_c[0]}]
set_property PACKAGE_PIN AA20 [get_ports {ddr4_cke[0]}]
set_property PACKAGE_PIN AF22 [get_ports {ddr4_cs_n[0]}]
set_property PACKAGE_PIN AB20 [get_ports {ddr4_odt[0]}]
set_property PACKAGE_PIN AE26 [get_ports {ddr4_reset_n}]
