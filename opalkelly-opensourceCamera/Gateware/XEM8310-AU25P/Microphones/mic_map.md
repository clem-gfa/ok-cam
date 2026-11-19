# Correspondance des 64 microphones (canal de l'appli ↔ MK ↔ ligne PDM ↔ broche)

Source : `DARWIN.pdf` (feuille Mics et 32 feuilles « Dual Mic n »), `mic_pins.xdc`. **Électrique uniquement : la position physique sur le PCB n'est pas dans ces fichiers.**

Règles lues sur le schéma (vérifiées sur les 32 feuilles) :
- la feuille « Dual Mic n » porte **MK(2n−1)** et **MK(2n)**, reliés à la même ligne `MicDatan` et à la même horloge ;
- MK(2n−1) : SELECT = +1,8 V (micro « haut », donnée sur front montant du PDM) ; MK(2n) : SELECT = GND (micro « bas », front descendant) ;
- horloge : `Clock_1` pour les feuilles 1–8, `Clock_2` pour 9–16, `Clock_3` pour 17–24, `Clock_4` pour 25–32 (inverseurs SN74LVC1GU04) ;
- **canal de l'appli = numéro MK** (canal 1 = MK1 … canal 64 = MK64), si la voie « haut » de la ligne est bien celle que le gateware range en canal impair (hypothèse de `pdm_capture.v`, **à confirmer en soufflant sur un micro**).

| Canal | Micro | Feuille | Ligne PDM | Port FPGA | MC2 | Broche FPGA | SELECT | Horloge |
|---|---|---|---|---|---|---|---|---|
| 1 | MK1 | Dual Mic 1 | MicData1 | `mic_data[0]` | MC2-66 | AA13 (B84_L11N) | VDD (haut) | Clock_1 |
| 2 | MK2 | Dual Mic 1 | MicData1 | `mic_data[0]` | MC2-66 | AA13 (B84_L11N) | GND (bas) | Clock_1 |
| 3 | MK3 | Dual Mic 2 | MicData2 | `mic_data[1]` | MC2-64 | Y13 (B84_L11P) | VDD (haut) | Clock_1 |
| 4 | MK4 | Dual Mic 2 | MicData2 | `mic_data[1]` | MC2-64 | Y13 (B84_L11P) | GND (bas) | Clock_1 |
| 5 | MK5 | Dual Mic 3 | MicData3 | `mic_data[2]` | MC2-62 | AA15 (B84_L7N_HDGC) | VDD (haut) | Clock_1 |
| 6 | MK6 | Dual Mic 3 | MicData3 | `mic_data[2]` | MC2-62 | AA15 (B84_L7N_HDGC) | GND (bas) | Clock_1 |
| 7 | MK7 | Dual Mic 4 | MicData4 | `mic_data[3]` | MC2-60 | Y15 (B84_L7P_HDGC) | VDD (haut) | Clock_1 |
| 8 | MK8 | Dual Mic 4 | MicData4 | `mic_data[3]` | MC2-60 | Y15 (B84_L7P_HDGC) | GND (bas) | Clock_1 |
| 9 | MK9 | Dual Mic 5 | MicData5 | `mic_data[4]` | MC2-58 | G9 (B86_L5N_HDGC) | VDD (haut) | Clock_1 |
| 10 | MK10 | Dual Mic 5 | MicData5 | `mic_data[4]` | MC2-58 | G9 (B86_L5N_HDGC) | GND (bas) | Clock_1 |
| 11 | MK11 | Dual Mic 6 | MicData6 | `mic_data[5]` | MC2-56 | G10 (B86_L5P_HDGC) | VDD (haut) | Clock_1 |
| 12 | MK12 | Dual Mic 6 | MicData6 | `mic_data[5]` | MC2-56 | G10 (B86_L5P_HDGC) | GND (bas) | Clock_1 |
| 13 | MK13 | Dual Mic 7 | MicData7 | `mic_data[6]` | MC2-54 | F9 (B86_L6N_HDGC) | VDD (haut) | Clock_1 |
| 14 | MK14 | Dual Mic 7 | MicData7 | `mic_data[6]` | MC2-54 | F9 (B86_L6N_HDGC) | GND (bas) | Clock_1 |
| 15 | MK15 | Dual Mic 8 | MicData8 | `mic_data[7]` | MC2-52 | F10 (B86_L6P_HDGC) | VDD (haut) | Clock_1 |
| 16 | MK16 | Dual Mic 8 | MicData8 | `mic_data[7]` | MC2-52 | F10 (B86_L6P_HDGC) | GND (bas) | Clock_1 |
| 17 | MK17 | Dual Mic 9 | MicData9 | `mic_data[8]` | MC2-50 | J10 (B86_L2N) | VDD (haut) | Clock_2 |
| 18 | MK18 | Dual Mic 9 | MicData9 | `mic_data[8]` | MC2-50 | J10 (B86_L2N) | GND (bas) | Clock_2 |
| 19 | MK19 | Dual Mic 10 | MicData10 | `mic_data[9]` | MC2-48 | J11 (B86_L2P) | VDD (haut) | Clock_2 |
| 20 | MK20 | Dual Mic 10 | MicData10 | `mic_data[9]` | MC2-48 | J11 (B86_L2P) | GND (bas) | Clock_2 |
| 21 | MK21 | Dual Mic 11 | MicData11 | `mic_data[10]` | MC2-46 | A9 (B86_L10N) | VDD (haut) | Clock_2 |
| 22 | MK22 | Dual Mic 11 | MicData11 | `mic_data[10]` | MC2-46 | A9 (B86_L10N) | GND (bas) | Clock_2 |
| 23 | MK23 | Dual Mic 12 | MicData12 | `mic_data[11]` | MC2-44 | B9 (B86_L10P) | VDD (haut) | Clock_2 |
| 24 | MK24 | Dual Mic 12 | MicData12 | `mic_data[11]` | MC2-44 | B9 (B86_L10P) | GND (bas) | Clock_2 |
| 25 | MK25 | Dual Mic 13 | MicData13 | `mic_data[12]` | MC2-42 | E10 (B86_L7N_HDGC) | VDD (haut) | Clock_2 |
| 26 | MK26 | Dual Mic 13 | MicData13 | `mic_data[12]` | MC2-42 | E10 (B86_L7N_HDGC) | GND (bas) | Clock_2 |
| 27 | MK27 | Dual Mic 14 | MicData14 | `mic_data[13]` | MC2-40 | E11 (B86_L7P_HDGC) | VDD (haut) | Clock_2 |
| 28 | MK28 | Dual Mic 14 | MicData14 | `mic_data[13]` | MC2-40 | E11 (B86_L7P_HDGC) | GND (bas) | Clock_2 |
| 29 | MK29 | Dual Mic 15 | MicData15 | `mic_data[14]` | MC2-38 | A10 (B86_L11N) | VDD (haut) | Clock_2 |
| 30 | MK30 | Dual Mic 15 | MicData15 | `mic_data[14]` | MC2-38 | A10 (B86_L11N) | GND (bas) | Clock_2 |
| 31 | MK31 | Dual Mic 16 | MicData16 | `mic_data[15]` | MC2-36 | B10 (B86_L11P) | VDD (haut) | Clock_2 |
| 32 | MK32 | Dual Mic 16 | MicData16 | `mic_data[15]` | MC2-36 | B10 (B86_L11P) | GND (bas) | Clock_2 |
| 33 | MK33 | Dual Mic 17 | MicData17 | `mic_data[16]` | MC2-32 | H12 (B87_L1N) | VDD (haut) | Clock_3 |
| 34 | MK34 | Dual Mic 17 | MicData17 | `mic_data[16]` | MC2-32 | H12 (B87_L1N) | GND (bas) | Clock_3 |
| 35 | MK35 | Dual Mic 18 | MicData18 | `mic_data[17]` | MC2-30 | J12 (B87_L1P) | VDD (haut) | Clock_3 |
| 36 | MK36 | Dual Mic 18 | MicData18 | `mic_data[17]` | MC2-30 | J12 (B87_L1P) | GND (bas) | Clock_3 |
| 37 | MK37 | Dual Mic 19 | MicData19 | `mic_data[18]` | MC2-28 | A12 (B87_L11N) | VDD (haut) | Clock_3 |
| 38 | MK38 | Dual Mic 19 | MicData19 | `mic_data[18]` | MC2-28 | A12 (B87_L11N) | GND (bas) | Clock_3 |
| 39 | MK39 | Dual Mic 20 | MicData20 | `mic_data[19]` | MC2-26 | A13 (B87_L11P) | VDD (haut) | Clock_3 |
| 40 | MK40 | Dual Mic 20 | MicData20 | `mic_data[19]` | MC2-26 | A13 (B87_L11P) | GND (bas) | Clock_3 |
| 41 | MK41 | Dual Mic 21 | MicData21 | `mic_data[20]` | MC2-24 | D13 (B87_L8N_HDGC) | VDD (haut) | Clock_3 |
| 42 | MK42 | Dual Mic 21 | MicData21 | `mic_data[20]` | MC2-24 | D13 (B87_L8N_HDGC) | GND (bas) | Clock_3 |
| 43 | MK43 | Dual Mic 22 | MicData22 | `mic_data[21]` | MC2-22 | D14 (B87_L8P_HDGC) | VDD (haut) | Clock_3 |
| 44 | MK44 | Dual Mic 22 | MicData22 | `mic_data[21]` | MC2-22 | D14 (B87_L8P_HDGC) | GND (bas) | Clock_3 |
| 45 | MK45 | Dual Mic 23 | MicData23 | `mic_data[22]` | MC2-20 | C13 (B87_L9N) | VDD (haut) | Clock_3 |
| 46 | MK46 | Dual Mic 23 | MicData23 | `mic_data[22]` | MC2-20 | C13 (B87_L9N) | GND (bas) | Clock_3 |
| 47 | MK47 | Dual Mic 24 | MicData24 | `mic_data[23]` | MC2-18 | C14 (B87_L9P) | VDD (haut) | Clock_3 |
| 48 | MK48 | Dual Mic 24 | MicData24 | `mic_data[23]` | MC2-18 | C14 (B87_L9P) | GND (bas) | Clock_3 |
| 49 | MK49 | Dual Mic 25 | MicData25 | `mic_data[24]` | MC2-16 | G14 (B87_L3N) | VDD (haut) | Clock_4 |
| 50 | MK50 | Dual Mic 25 | MicData25 | `mic_data[24]` | MC2-16 | G14 (B87_L3N) | GND (bas) | Clock_4 |
| 51 | MK51 | Dual Mic 26 | MicData26 | `mic_data[25]` | MC2-14 | H14 (B87_L3P) | VDD (haut) | Clock_4 |
| 52 | MK52 | Dual Mic 26 | MicData26 | `mic_data[25]` | MC2-14 | H14 (B87_L3P) | GND (bas) | Clock_4 |
| 53 | MK53 | Dual Mic 27 | MicData27 | `mic_data[26]` | MC2-12 | H13 (B87_L2N) | VDD (haut) | Clock_4 |
| 54 | MK54 | Dual Mic 27 | MicData27 | `mic_data[26]` | MC2-12 | H13 (B87_L2N) | GND (bas) | Clock_4 |
| 55 | MK55 | Dual Mic 28 | MicData28 | `mic_data[27]` | MC2-10 | J13 (B87_L2P) | VDD (haut) | Clock_4 |
| 56 | MK56 | Dual Mic 28 | MicData28 | `mic_data[27]` | MC2-10 | J13 (B87_L2P) | GND (bas) | Clock_4 |
| 57 | MK57 | Dual Mic 29 | MicData29 | `mic_data[28]` | MC2-9 | J15 (B87_L4P) | VDD (haut) | Clock_4 |
| 58 | MK58 | Dual Mic 29 | MicData29 | `mic_data[28]` | MC2-9 | J15 (B87_L4P) | GND (bas) | Clock_4 |
| 59 | MK59 | Dual Mic 30 | MicData30 | `mic_data[29]` | MC2-11 | J14 (B87_L4N) | VDD (haut) | Clock_4 |
| 60 | MK60 | Dual Mic 30 | MicData30 | `mic_data[29]` | MC2-11 | J14 (B87_L4N) | GND (bas) | Clock_4 |
| 61 | MK61 | Dual Mic 31 | MicData31 | `mic_data[30]` | MC2-13 | B14 (B87_L12P) | VDD (haut) | Clock_4 |
| 62 | MK62 | Dual Mic 31 | MicData31 | `mic_data[30]` | MC2-13 | B14 (B87_L12P) | GND (bas) | Clock_4 |
| 63 | MK63 | Dual Mic 32 | MicData32 | `mic_data[31]` | MC2-15 | A14 (B87_L12N) | VDD (haut) | Clock_4 |
| 64 | MK64 | Dual Mic 32 | MicData32 | `mic_data[31]` | MC2-15 | A14 (B87_L12N) | GND (bas) | Clock_4 |

## Position sur le PCB

Les micros sont des SPH0641LU4H-1 **bottom port** : le son entre par un trou du PCB, côté face arrière. La vue utile pour l'écoute est donc celle **côté port acoustique**, qui est le **miroir gauche-droite** de la vue KiCad (côté composants).

- `mic_layout_acoustique.svg` : **vue principale** : côté port acoustique (face arrière, miroir de la vue KiCad), puis tournée de **90° vers la droite** pour suivre l'horizontale de la caméra. Les graduations donnent les coordonnées KiCad (x à gauche, y en bas).
- `mic_layout_acoustique_miroir.svg` : la même vue sans la rotation de 90°.
- `mic_layout_composants.svg` : vue du dessus, côté composants, comme dans KiCad.
- Régénération : `python3 gen_mic_layout.py` (lit `mic_map.csv`).

D'après `DARWIN.kicad_pcb` (carte 170 × 100 mm, origine KiCad (50, 50) mm) : les micros forment une spirale autour du trou Ø6 en (100, 100), tous sur la face avant. Les coordonnées (centre du composant, mm, repère KiCad) sont dans les colonnes `x_mm`, `y_mm` de `mic_map.csv`. Les micros d'une même paire (2n−1, 2n) ne sont pas voisins.
