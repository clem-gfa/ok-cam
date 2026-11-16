#----------------------------------------------------------------------
# add_userio.tcl - LED et boutons utilisateur de la carte DARWIN, accessibles depuis le PC.
#
# NON TESTE sous Vivado (ecrit sans acces a l'outil).
# Utilisation, avant de sourcer project.tcl :   set with_userio 1
# Ajoute un AXI GPIO (IP Xilinx) derriere le pont AXI-Lite existant (smartconnect_1, un maitre de plus) :
#   base 0x40000000  GPIO_DATA  (0x00) : bit0 = LED_0, bit1 = LED_1 (1 = allumee), lecture/ecriture
#                    GPIO2_DATA (0x08) : bit0 = User_Button, bit1 = Reset_Button (lus a 0 quand appuyes)
# Ports : user_leds_tri_o[1:0], user_buttons_tri_i[1:0] (contraintes : xem8310-au25p_userio.xdc).
# Les registres sont lus par les pilotes UserIoDriver (C++ : Software/Cxx/src, TypeScript : Software/FP-Platform/src).
#----------------------------------------------------------------------

add_files -fileset constrs_1 -norecurse xem8310-au25p_userio.xdc

set axi_gpio_userio [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_gpio axi_gpio_userio ]
set_property -dict [list \
    CONFIG.C_GPIO_WIDTH {2} \
    CONFIG.C_ALL_OUTPUTS {1} \
    CONFIG.C_DOUT_DEFAULT {0x00000000} \
    CONFIG.C_IS_DUAL {1} \
    CONFIG.C_GPIO2_WIDTH {2} \
    CONFIG.C_ALL_INPUTS_2 {1} \
] $axi_gpio_userio

# Un maitre de plus sur le smartconnect du pont AXI-Lite
set mi [get_property CONFIG.NUM_MI [get_bd_cells smartconnect_1]]
set_property CONFIG.NUM_MI [expr {$mi + 1}] [get_bd_cells smartconnect_1]
connect_bd_intf_net [get_bd_intf_pins smartconnect_1/[format "M%02d_AXI" $mi]] [get_bd_intf_pins axi_gpio_userio/S_AXI]

connect_bd_net [get_bd_pins frontpanel_0/okClk]                 [get_bd_pins axi_gpio_userio/s_axi_aclk]
connect_bd_net [get_bd_pins axi_reset_iwrap_0/axil_aresetn]     [get_bd_pins axi_gpio_userio/s_axi_aresetn]

set user_leds    [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:gpio_rtl:1.0 user_leds ]
set user_buttons [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:gpio_rtl:1.0 user_buttons ]
connect_bd_intf_net [get_bd_intf_pins axi_gpio_userio/GPIO]  $user_leds
connect_bd_intf_net [get_bd_intf_pins axi_gpio_userio/GPIO2] $user_buttons

assign_bd_address -offset 0x40000000 -range 0x00010000 -target_address_space [get_bd_addr_spaces fp_to_axil_iwrap_0/m_axil] [get_bd_addr_segs axi_gpio_userio/S_AXI/Reg] -force

puts "add_userio.tcl : AXI GPIO LED/boutons ajoute a 0x40000000 (smartconnect_1 M[format %02d $mi])."
