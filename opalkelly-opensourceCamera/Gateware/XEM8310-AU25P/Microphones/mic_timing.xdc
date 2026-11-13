# Contraintes de timing des micros (a verifier apres la premiere implementation).
# Les lignes MicData sont echantillonnees par un registre d'IOB sur l'horloge micros ; le delai entre le front
# d'horloge sorti par le FPGA et la donnee du micro (18-40 ns + routage carte) n'est pas modelisable par le STA :
# la phase d'echantillonnage est reglee par le delai SD (registre WireIn 0x01, fenetre OK constatee en simulation :
# 3 a 8 cycles sur 10) et recalee sur la carte.
set_false_path -from [get_ports {mic_data[*]}]
set_false_path -to   [get_ports mic_clk]
# Sortie d'horloge micros : debit eleve, pas de terminaison
set_property SLEW FAST [get_ports mic_clk]
