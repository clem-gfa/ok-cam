# LED et boutons utilisateur de la carte DARWIN (connecteur MC1, bank 84, VCCO = VIO2 = 1,8 V).
# Source : DARWIN.pdf page 2 (connecteur MC1) + xem8310.xdc officiel (MC1-n -> PACKAGE_PIN).
# Attention a la lecture du plan : le numero d'une broche est dessine AU-DESSUS de sa ligne (corrige apres
# un premier essai sur carte ou les broches etaient decalees de deux).
#   LED : FPGA -> 10 R -> LED rouge -> GND : allumee a 1.
#   Boutons : pull-up 4,75 k vers 1,8 V + filtre RC 100 k / 0,1 uF (~10 ms) : 1 au repos, 0 appuye.

# LED_0 : MC1-67 (B84_L6N_HDGC)
set_property PACKAGE_PIN AB16 [get_ports {user_leds_tri_o[0]}]
# LED_1 : MC1-65 (B84_L6P_HDGC)
set_property PACKAGE_PIN AB15 [get_ports {user_leds_tri_o[1]}]
set_property IOSTANDARD LVCMOS18 [get_ports {user_leds_tri_o[*]}]
set_property DRIVE 4 [get_ports {user_leds_tri_o[*]}]
set_property SLEW SLOW [get_ports {user_leds_tri_o[*]}]

# User_Button : MC1-61 (B84_L4P)
set_property PACKAGE_PIN AD13 [get_ports {user_buttons_tri_i[0]}]
# Reset_Button : MC1-63 (B84_L4N) : simple entree utilisateur, ne remet rien a zero
set_property PACKAGE_PIN AD14 [get_ports {user_buttons_tri_i[1]}]
set_property IOSTANDARD LVCMOS18 [get_ports {user_buttons_tri_i[*]}]
# Signaux lents et asynchrones : pas de contrainte de timing
set_false_path -from [get_ports {user_buttons_tri_i[*]}]
set_false_path -to   [get_ports {user_leds_tri_o[*]}]
