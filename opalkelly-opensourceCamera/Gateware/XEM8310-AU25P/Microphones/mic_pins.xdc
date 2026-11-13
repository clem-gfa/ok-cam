# Microphones SPH0641LU4H-1 (32 lignes PDM, 2 micros par ligne) sur le connecteur MC2 de la XEM8310
# Source : DARWIN.pdf (page 2, connecteur MC2) + xem8310.xdc officiel (MC2-n -> PACKAGE_PIN).
# ATTENTION : les banques 86/87 sont alimentees par VIO3 (3,3 V par defaut), la banque 84 par VIO2.
# Les micros sont en 1,8 V : regler VIO3 a 1,8 V (reglage FrontPanel, comme VIO2) avant d'utiliser LVCMOS18.

# MicClock  MC2-17  B87_L6P_HDGC
set_property PACKAGE_PIN F14 [get_ports {mic_clk}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_clk}]
set_property SLEW FAST [get_ports {mic_clk}]
set_property DRIVE 8 [get_ports {mic_clk}]
# MicData1  MC2-66  B84_L11N
set_property PACKAGE_PIN AA13 [get_ports {mic_data[0]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[0]}]
# MicData2  MC2-64  B84_L11P
set_property PACKAGE_PIN Y13 [get_ports {mic_data[1]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[1]}]
# MicData3  MC2-62  B84_L7N_HDGC
set_property PACKAGE_PIN AA15 [get_ports {mic_data[2]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[2]}]
# MicData4  MC2-60  B84_L7P_HDGC
set_property PACKAGE_PIN Y15 [get_ports {mic_data[3]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[3]}]
# MicData5  MC2-58  B86_L5N_HDGC
set_property PACKAGE_PIN G9 [get_ports {mic_data[4]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[4]}]
# MicData6  MC2-56  B86_L5P_HDGC
set_property PACKAGE_PIN G10 [get_ports {mic_data[5]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[5]}]
# MicData7  MC2-54  B86_L6N_HDGC
set_property PACKAGE_PIN F9 [get_ports {mic_data[6]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[6]}]
# MicData8  MC2-52  B86_L6P_HDGC
set_property PACKAGE_PIN F10 [get_ports {mic_data[7]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[7]}]
# MicData9  MC2-50  B86_L2N
set_property PACKAGE_PIN J10 [get_ports {mic_data[8]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[8]}]
# MicData10  MC2-48  B86_L2P
set_property PACKAGE_PIN J11 [get_ports {mic_data[9]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[9]}]
# MicData11  MC2-46  B86_L10N
set_property PACKAGE_PIN A9 [get_ports {mic_data[10]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[10]}]
# MicData12  MC2-44  B86_L10P
set_property PACKAGE_PIN B9 [get_ports {mic_data[11]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[11]}]
# MicData13  MC2-42  B86_L7N_HDGC
set_property PACKAGE_PIN E10 [get_ports {mic_data[12]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[12]}]
# MicData14  MC2-40  B86_L7P_HDGC
set_property PACKAGE_PIN E11 [get_ports {mic_data[13]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[13]}]
# MicData15  MC2-38  B86_L11N
set_property PACKAGE_PIN A10 [get_ports {mic_data[14]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[14]}]
# MicData16  MC2-36  B86_L11P
set_property PACKAGE_PIN B10 [get_ports {mic_data[15]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[15]}]
# MicData17  MC2-32  B87_L1N
set_property PACKAGE_PIN H12 [get_ports {mic_data[16]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[16]}]
# MicData18  MC2-30  B87_L1P
set_property PACKAGE_PIN J12 [get_ports {mic_data[17]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[17]}]
# MicData19  MC2-28  B87_L11N
set_property PACKAGE_PIN A12 [get_ports {mic_data[18]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[18]}]
# MicData20  MC2-26  B87_L11P
set_property PACKAGE_PIN A13 [get_ports {mic_data[19]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[19]}]
# MicData21  MC2-24  B87_L8N_HDGC
set_property PACKAGE_PIN D13 [get_ports {mic_data[20]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[20]}]
# MicData22  MC2-22  B87_L8P_HDGC
set_property PACKAGE_PIN D14 [get_ports {mic_data[21]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[21]}]
# MicData23  MC2-20  B87_L9N
set_property PACKAGE_PIN C13 [get_ports {mic_data[22]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[22]}]
# MicData24  MC2-18  B87_L9P
set_property PACKAGE_PIN C14 [get_ports {mic_data[23]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[23]}]
# MicData25  MC2-16  B87_L3N
set_property PACKAGE_PIN G14 [get_ports {mic_data[24]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[24]}]
# MicData26  MC2-14  B87_L3P
set_property PACKAGE_PIN H14 [get_ports {mic_data[25]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[25]}]
# MicData27  MC2-12  B87_L2N
set_property PACKAGE_PIN H13 [get_ports {mic_data[26]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[26]}]
# MicData28  MC2-10  B87_L2P
set_property PACKAGE_PIN J13 [get_ports {mic_data[27]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[27]}]
# MicData29  MC2-9  B87_L4P
set_property PACKAGE_PIN J15 [get_ports {mic_data[28]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[28]}]
# MicData30  MC2-11  B87_L4N
set_property PACKAGE_PIN J14 [get_ports {mic_data[29]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[29]}]
# MicData31  MC2-13  B87_L12P
set_property PACKAGE_PIN B14 [get_ports {mic_data[30]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[30]}]
# MicData32  MC2-15  B87_L12N
set_property PACKAGE_PIN A14 [get_ports {mic_data[31]}]
set_property IOSTANDARD LVCMOS18 [get_ports {mic_data[31]}]
