# Microphones SPH0641LU4H-1 (64 micros, 32 lignes PDM) — XEM8310 / DARWIN

Etat : **etude et prototype** (pas encore de gateware). Contenu :

- `mic_pins.xdc` : contraintes de broches (33 signaux sur le connecteur **MC2** de la XEM8310).
- `sim/pdm_chain.py` : modele de la chaine de decimation PDM -> PCM (numpy uniquement).
- `rtl/` : `mic_clkgen.v` (horloge PDM, sequence standard puis ultrason, strobes d'echantillonnage), `pdm_capture.v` (32 lignes -> 64 bits), `cic_bank.v` (64 CIC ordre 5, R=12, 19 bits), `fir_bank.v` (FIR de decimation x2, 73 coefficients Q17 symetriques, 8 voies x 8 canaux, 1 multiplieur par voie), `fir_coeffs.vh` (genere par `sim/gen_fir_coeffs.py`), `audio_packetizer.v` (paquets de 4096 octets pour le PC), `ts_unit.v` (base de temps commune audio/video), `mic_subsystem.v` (assemble le tout derriere des interfaces FrontPanel : WireIn 0x01, WireOut 0x20-0x23 (0x23 = occupation du FIFO audio en mots), pipe 0xA1).
- `mic_timing.xdc` : contraintes (faux chemins sur les pads micros, delai reglé par SD).
- `../IMX219/add_mics.tcl` : ajoute le sous-systeme micros au projet IMX219 (voir « Integration Vivado »).
- `sim/run.sh [SD] [INVERT]` : simulation iverilog (64 micros modelises avec les timings de la datasheet) + comparaison bit a bit avec le modele Python.

## Etat de la verification (simulation uniquement, pas encore sur carte)
- Capture + CIC : 64/64 canaux identiques au modele, horloge normale et inversee.
- Fenetre de delai d'echantillonnage SD (96 MHz) : 0-2 echec, **3 a 8 OK** ; valeur par defaut 7 (a recaler sur la carte, le modele ne connait pas les delais de routage).
- FIR (`sim/run_fir.sh`) : 64/64 canaux identiques au modele entier, cadence reelle (une entree toutes les 240 cycles a 96 MHz, 336 cycles de calcul pour 480 disponibles a 96 MHz ; avec la vraie horloge 99,96 MHz / 22, 528 disponibles). Gain verifie : sinus 0,5 -> crete 3,98 M (attendu 3,98 M), pleine echelle sans depassement des 24 bits.
- Paquets (`sim/run_audio.sh`) : en-tetes, charge utile, horodatages et abandon de paquets entiers quand le PC ne lit plus (45 paquets source, 9 abandonnes, seq avec trous, flag « perdu » coherent, aucun paquet tronque).
- Bout en bout (`sim/run_subsystem.sh`) : 64 micros modelises -> `mic_subsystem` -> pipe 0xA1 lue par un « PC » (blocs de 1024 mots) : 6 paquets, 126 instants, **64/64 canaux identiques** au modele (CIC puis FIR entiers), aucun paquet abandonne, statut `0x7` (run, ultra, locked).
- Logiciel PC (C++, `Software/Cxx/src/AudioStream.h` + `AudioPipe.h`, test `Software/Cxx/tests/audio_stream_test.cpp`, `-DBUILD_TESTS=ON`) : decodage des paquets, fichier **WAV RF64 64 canaux 24 bits** (valide au-dela de 4 Go, relu correctement par `ffprobe`), trous de `seq` remplaces par du silence et comptes, fichier `.csv` des horodatages par paquet (alignement avec la video), fenetre des derniers echantillons pour l'affichage. Teste hors materiel avec une pipe simulee (48 paquets dont 3 perdus). `AudioPipeOverClassicDataPort` (lecture reelle de la pipe 0xA1) n'est pas compile ici (pas de SDK FrontPanel).
- Non fait : egalisation par micro, interface graphique (formes d'onde).

## Integration Vivado (`IMX219/add_mics.tcl`, NON TESTE sous Vivado)
```
set fpdir <chemin du FrontPanel Vivado IP>
set with_mics 1
source project.tcl
```
Sans `with_mics`, le projet camera est inchange. Le script ajoute : le FIFO audio (horloges independantes, 32768 x 32 = 128 Kio, ~3,6 ms de flux), la sortie `clk_wiz_0/clk_out2` (99,96 MHz) comme horloge micros, **sans second MMCM** (un second MMCM sur okClk echoue au placement : Place 30-718, cascade MMCM -> BUFGCE -> 2 MMCM) ; horloge PDM = 99,96 / 22 = 4,544 MHz (maxi du micro : 4,8 MHz), fs = 189,3 kS/s, `mic_subsystem`, les endpoints FrontPanel et les ports `mic_clk` / `mic_data[31:0]`, `mic_pins.xdc` et `mic_timing.xdc`. Points a verifier au premier essai : noms des pins d'interface du FrontPanel (`wirein01`, `wireout20..22`, `btpipeouta1`), parametres du FIFO, DRC de banque (VIO3).
Mise en service : VIO3 = 1,8 V, ecrire WireIn 0x01 = reset (0x2) puis enable | SD<<8 (SD = 7 au depart), lire WireOut 0x20 (bit0 run), puis lire la pipe 0xA1 avec le meme bloc que `BLOCK_WORDS` de `mic_subsystem` (defaut 256 mots = 1024 octets, comme la video ; le PC lit des multiples de 4096 octets = 1 paquet).
Le debut de trame video est detecte par `axis_sof_monitor.v` (observateur AXI-Stream sur le flux vers le VDMA, simulation `sim/tb_sof.v` OK) : `add_mics.tcl` le branche sur `axi_vdma_0/S_AXIS_S2MM` ; si ce branchement echoue, le script le signale et tient `vsof` a 0 (`vcnt` reste 0).

## Transport vers le PC (choix propose)
Une **seconde pipe FrontPanel a blocs, adresse 0xA1** (`CONFIG.BTPO.COUNT {2}`, `BTPO.ADDR_1 {0xA1}`), alimentee par un FIFO comme `axis_to_btpipeout_shim` (video, 0xA0), avec `space_ok` = FIFO libre >= 1024 mots. Bloc de 4096 octets = 1 paquet. Debit : 21 instants/paquet x 192 octets, ~9,5 k paquets/s = ~39 Mo/s.
Format du paquet (1024 mots 32 bits, little-endian) : 8 mots d'en-tete (magic `MIC1`, seq, horodatage 64 bits du 1er instant, nsamp + flags, nombre de debuts de trame video, horodatage du dernier debut de trame video), puis 21 x 48 mots (64 canaux x 24 bits, canal c aux bits [24c +: 24]), puis 8 mots de bourrage. Voir l'en-tete de `rtl/audio_packetizer.v`.
Synchro video : `ts_unit` mesure, sur la meme horloge que l'audio, le debut de chaque trame video (SOF, hors domaine d'horloge, incertitude de quelques cycles). **A faire** : brancher son `vsof` sur le debut de trame du flux camera (le SOF du flux AXI-Stream vers le VDMA) et exposer la frequence de l'horloge audio (ticks par seconde) sur un registre.


## Parametres retenus (voir datasheet Knowles, rev. B)
- Mode **ultrason** : horloge 3,072 a 4,8 MHz. Choix : **99,96 MHz / 22 = 4,544 MHz** (horloge deja presente dans le design, rapport cyclique 50 %) ; mode standard de demarrage : 99,96 / 44 = 2,272 MHz (< 2,475 MHz).
- Bande utile **80 kHz** (reponse ultrason specifiee seulement jusqu'a 80 kHz, avec un pic de ~+14 dB vers 25 kHz : egalisation necessaire).
- Decimation x24 : CIC ordre 5 R=12 (19 bits) -> 378,6 kS/s, puis FIR (63 + 10 coefficients) decimation 2 -> **189,3 kS/s**, 24 bits (bande utile 80 kHz : -1,2 dB a 80 kHz, -8,6 dB a 100 kHz).
- Debit utile : 64 x 189,3 kS/s x 3 octets = **36,3 Mo/s** (24,2 Mo/s en 16 bits). Un paquet = 21 instants = 528 ticks x 21 ; ~9 k paquets/s.
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

## Appli TypeScript (`Software/FP-Platform`)
- `src/AudioStream.ts` : decodage des paquets, WAV RF64 64 canaux 24 bits **par segments** (60 s par defaut), CSV d'horodatage, trous remplaces par du silence, fenetre d'affichage ; testé en Node (`npm run test:audio`, relu par `ffprobe`).
- `src/AudioPipe.ts` : pipe 0xA1 + WireIn 0x01 / WireOut 0x20-0x23 ; `micPresent()` verifie que le bitfile contient les micros (sinon lire la pipe bloquerait la file du device).
- `src/OpfsStorage.ts` : l'hote FrontPanel refuse `showSaveFilePicker` / `showDirectoryPicker` ; on ecrit dans l'OPFS (~320 Mo/s, quota ~6 Go) et on exporte chaque segment termine par `<a download>` (un `File` de l'OPFS, pas un Blob en memoire : celui-ci arrive vide). Les 4 derniers fichiers exportes restent dans l'OPFS puis sont supprimes.
- `src/AudioPanel.tsx` : bouton « Audio » (en bas a gauche) -> panneau avec demarrage/arret des micros, 64 formes d'onde glissantes (8 x 8, enveloppe min/max, fenetre 5/20/100 ms, gain x1..x512), enregistrement.
- **Entrelacement audio/video** : une image video est lue en un seul transfert de plusieurs dizaines de ms, alors que le FIFO audio ne contient que quelques ms. `AxiStreamOverClassicDataPort` lit donc les gros transferts par tranches de 128 Kio et appelle `AudioRecorder.pump()` entre deux tranches (lecture de ce qui est disponible, sans attente, grace a l'occupation du FIFO lue sur le WireOut 0x23). Sans cela, presque tous les paquets audio seraient perdus des que la video tourne.
- Non teste sur le materiel (le gateware micros n'est pas encore construit/valide).

### Pertes de paquets (premiere mesure sur carte)
Premiere mesure : 18 % de paquets perdus avec un dessin lourd, puis 7,9 % apres allegement du dessin ; le compteur FPGA `dropped` (~30 900) est presque egal aux paquets perdus cote appli (~31 300) : ce sont des **debordements du FIFO du FPGA** (3,6 ms de marge), pas des erreurs de transport. Le panneau affiche maintenant le plus long intervalle entre deux services de l'audio (`host gap max`) et le nombre d'intervalles de plus de 3 ms. Remede materiel : FIFO plus profond (`set mic_fifo_aw 16` ou `17` avant `source project.tcl`, 256 Kio ~7 ms / 512 Kio ~14 ms, a verifier cote BRAM) ; remede logiciel : moins de travail sur le thread principal de la page pendant l'acquisition.

### Tampon en anneau en DDR4 (`USE_DDR = 1`, defaut de `add_mics.tcl`)
Mesure sur carte avec FIFO de 256 Kio (`mic_fifo_aw 16`) : 11,4 % de paquets perdus, blocage maximal de la page 226 ms, ~1140 blocages de plus de 3 ms en 38 s (un par image video) : un FIFO de quelques ms ne suffit pas. Les paquets passent donc par la DDR4 :
```
paquetiseur -> FIFO d'entree (32 -> 128 bits, FWFT) -> [AXI ecriture] anneau en DDR4 [AXI lecture] -> FIFO de sortie (128 -> 32 bits) -> pipe 0xA1
```
- `rtl/audio_ddr_ring.v` (module de reference du block design **a une seule horloge**, separe de `mic_subsystem` : dans un module a plusieurs horloges Vivado associait `m_axi` a deux horloges, BD 41-1732 / 41-237) : maitre AXI4 128 bits dans le domaine `c0_ddr4_ui_clk`, une rafale de 256 beats (= 1 paquet de 4 Kio, alignee 4 Kio) par transaction, anneau de 2^`mic_ring_aw` paquets (14 -> 64 Mio -> ~1,8 s de flux) a `0xA0000000` (plage de la DDR4, loin des tampons video). Un paquet n'est compte qu'apres la reponse d'ecriture ; un paquet n'est abandonne (entier) que si l'anneau est plein.
- Reset : les FIFO ne sont vides que lorsque l'anneau est au repos (jamais en pleine rafale AXI, ce qui bloquerait le bus DDR4 de la video).
- `add_mics.tcl` cree l'instance `audio_ddr_ring_0`, l'ajoute comme 3e maitre de `smartconnect_0` (S02_AXI), la connecte a `c0_ddr4_ui_clk` / au reset de la DDR4 comme le VDMA, relie 13 signaux simples a `mic_subsystem_0` et assigne l'espace d'adresses de la DDR4. `set mic_use_ddr 0` retablit l'ancien chemin (un seul FIFO).
- WireOut 0x24 = {pic, courant} de paquets dans l'anneau (le panneau affiche le retard maximal en ms).
- Simulation : `sim/run_subsystem_ddr.sh` (modele AXI a delais aleatoires, anneau de 16 paquets, le « PC » s'arrete ~12 paquets) : 24 paquets, 64/64 canaux identiques au modele, 0 paquet abandonne, 0 erreur AXI ; `sim/tb_ddr_ring.v` : plusieurs tours d'anneau, paquets abandonnes entiers quand tout est plein.
- **FIFO du chemin DDR4 = macros XPM** (`rtl/fifo_audio_stage.v` 32 -> 128 bits FWFT, `rtl/fifo_audio_out.v` 128 -> 32 bits) : l'IP FIFO Generator de cette version de Vivado refuse une largeur de lecture differente de la largeur d'ecriture par script (`Output_Data_Width` desactive, meme en plusieurs etapes). Le `rst` d'un `xpm_fifo_async` est synchrone a son `wr_clk` : chaque FIFO est remis a zero dans le domaine de son horloge d'ecriture, uniquement quand l'anneau est au repos. La simulation utilise `sim/fifo_ddr_models.v` (memes interfaces) : les macros XPM elles-memes ne sont pas simulees ici.
- **Non teste sous Vivado ni sur la carte** : l'inference de l'interface `m_axi`, les macros XPM (parametres, `USE_ADV_FEATURES`), le 3e port du smartconnect.
