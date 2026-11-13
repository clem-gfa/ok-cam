# Microphones SPH0641LU4H-1 (64 micros, 32 lignes PDM) — XEM8310 / DARWIN

Etat : **etude et prototype** (pas encore de gateware). Contenu :

- `mic_pins.xdc` : contraintes de broches (33 signaux sur le connecteur **MC2** de la XEM8310).
- `sim/pdm_chain.py` : modele de la chaine de decimation PDM -> PCM (numpy uniquement).
- `rtl/` : `mic_clkgen.v` (horloge PDM, sequence standard puis ultrason, strobes d'echantillonnage), `pdm_capture.v` (32 lignes -> 64 bits), `cic_bank.v` (64 CIC ordre 5, R=12, 19 bits).
- `sim/run.sh [SD] [INVERT]` : simulation iverilog (64 micros modelises avec les timings de la datasheet) + comparaison bit a bit avec le modele Python.

## Etat de la verification (simulation uniquement, pas encore sur carte)
- Capture + CIC : 64/64 canaux identiques au modele, horloge normale et inversee.
- Fenetre de delai d'echantillonnage SD (96 MHz) : 0-2 echec, **3 a 8 OK** ; valeur par defaut 7 (a recaler sur la carte, le modele ne connait pas les delais de routage).
- Non fait : FIR de decimation (HDL), egalisation par micro, paquets audio + horodatage, integration Vivado.

## Parametres retenus (voir datasheet Knowles, rev. B)
- Mode **ultrason** : horloge 3,072 a 4,8 MHz. Choix : **4,8 MHz**.
- Bande utile **80 kHz** (reponse ultrason specifiee seulement jusqu'a 80 kHz, avec un pic de ~+14 dB vers 25 kHz : egalisation necessaire).
- Decimation x24 : CIC ordre 5 R=12 (19 bits) -> 400 kS/s, puis FIR (63 + 10 coefficients) decimation 2 -> **200 kS/s**, 24 bits.
- Debit utile : 64 x 200 kS/s x 3 octets = **38,4 Mo/s** (25,6 Mo/s en 16 bits).
- Datasheet : ne pas demarrer directement en mode ultrason (ni sortir du sleep vers l'ultrason) : demarrer l'horloge en mode standard (1,024 a 2,475 MHz), puis passer a 4,8 MHz (delai de changement de mode 10 ms). Rapport cyclique 48-52 % au-dessus de 2,4 MHz.
- Micro « haut » (SELECT = VDD) : donnee sur front montant, a capturer sur front descendant ; micro « bas » (SELECT = GND) : l'inverse. `tDD` = 18 a 40 ns.

## Point d'attention : tension des banques
Les banques HD **86 et 87** (MicData5..32 et MicClock) ont leur VCCO sur **VIO3** (3,3 V par defaut d'apres DARWIN), la banque **84** (MicData1..4) sur VIO2. Les micros sont alimentes en 1,8 V (VOH min = VDD - 0,45 V = 1,35 V) : en LVCMOS33, VIH min = 2 V, la lecture est donc **non fiable**. Il faut regler VIO3 a 1,8 V (le module a un regulateur VIO3 pilote comme VIO2 ; le nom exact du reglage FrontPanel est a verifier) et verifier que rien d'autre sur DARWIN ne depend d'un VIO3 a 3,3 V. Le XDC est ecrit en LVCMOS18.
Autres points a verifier : les buffers d'horloge `SN74LVC1GU04` sont des inverseurs (polarite), et les lignes de donnees sont reparties sur 3 banques.

## Resultats du modele (sim/pdm_chain.py)
Modulateur sigma-delta generique d'ordre 2 : valide les filtres, **pas** le bruit du micro reel.

| Frequence | Gain |
|---|---|
| 1 / 10 / 25 / 40 kHz | 0,0 / -0,02 / -0,08 / -0,19 dB |
| 60 / 79 kHz | -0,45 / -1,03 dB |

Repliement : tons a 125 et 140 kHz attenues d'environ 73 dB une fois replies. La compensation du CIC (11 coefficients) reste a affiner (bord de bande a -1 dB, leger exces a 80 kHz).

## Brochage (MC2 -> FPGA)
| Signal | Broche MC2 | Net XEM8310 | Broche FPGA | Banque |
|---|---|---|---|---|
| MicClock | 17 | B87_L6P_HDGC | F14 | 87 |
| MicData1 | 66 | B84_L11N | AA13 | 84 |
| MicData2 | 64 | B84_L11P | Y13 | 84 |
| MicData3 | 62 | B84_L7N_HDGC | AA15 | 84 |
| MicData4 | 60 | B84_L7P_HDGC | Y15 | 84 |
| MicData5 | 58 | B86_L5N_HDGC | G9 | 86 |
| MicData6 | 56 | B86_L5P_HDGC | G10 | 86 |
| MicData7 | 54 | B86_L6N_HDGC | F9 | 86 |
| MicData8 | 52 | B86_L6P_HDGC | F10 | 86 |
| MicData9 | 50 | B86_L2N | J10 | 86 |
| MicData10 | 48 | B86_L2P | J11 | 86 |
| MicData11 | 46 | B86_L10N | A9 | 86 |
| MicData12 | 44 | B86_L10P | B9 | 86 |
| MicData13 | 42 | B86_L7N_HDGC | E10 | 86 |
| MicData14 | 40 | B86_L7P_HDGC | E11 | 86 |
| MicData15 | 38 | B86_L11N | A10 | 86 |
| MicData16 | 36 | B86_L11P | B10 | 86 |
| MicData17 | 32 | B87_L1N | H12 | 87 |
| MicData18 | 30 | B87_L1P | J12 | 87 |
| MicData19 | 28 | B87_L11N | A12 | 87 |
| MicData20 | 26 | B87_L11P | A13 | 87 |
| MicData21 | 24 | B87_L8N_HDGC | D13 | 87 |
| MicData22 | 22 | B87_L8P_HDGC | D14 | 87 |
| MicData23 | 20 | B87_L9N | C13 | 87 |
| MicData24 | 18 | B87_L9P | C14 | 87 |
| MicData25 | 16 | B87_L3N | G14 | 87 |
| MicData26 | 14 | B87_L3P | H14 | 87 |
| MicData27 | 12 | B87_L2N | H13 | 87 |
| MicData28 | 10 | B87_L2P | J13 | 87 |
| MicData29 | 9 | B87_L4P | J15 | 87 |
| MicData30 | 11 | B87_L4N | J14 | 87 |
| MicData31 | 13 | B87_L12P | B14 | 87 |
| MicData32 | 15 | B87_L12N | A14 | 87 |
