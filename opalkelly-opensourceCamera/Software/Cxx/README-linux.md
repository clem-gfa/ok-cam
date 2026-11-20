# Application C++ sous Linux (NUC x86-64, Raspberry Pi 5 ARM64)

Cibles officielles FrontPanel 6.1 ([notes de version](https://docs.opalkelly.com/fpsdk/release-notes/)) : Ubuntu 24.04 / 22.04 (x64), Rocky 10.1 / 9.3
(x64), **Raspberry Pi OS 12 « Bookworm » (ARM64, gcc 12.2)**. L'API C/C++ est fournie pour ces systèmes ; l'application FrontPanel
Platform (qui sert à *télécharger* le SDK) n'existe pas sous Raspbian : télécharger le Development Kit sur un autre PC puis copier le
dossier sur le Raspberry Pi.

## Paquets

```bash
sudo apt install build-essential cmake git libwxgtk3.2-dev libgtk-3-dev ffmpeg
```
Bookworm fournit CMake 3.25 et wxWidgets 3.2 ; Ubuntu 24.04 aussi (wx 3.2).

## SDK FrontPanel

1. Sur un PC avec FrontPanel Platform : bouton « Download Development Kit » -> dossier `FrontPanel-DevKit-6.1.0`.
2. Le copier sur la machine Linux. Le dossier `API/lib/` contient les bibliothèques de toutes les plateformes : vérifier le nom du
   sous-dossier ARM64 (`linux-arm64` attendu ; le CMake essaie aussi `linux-aarch64` et `linux-arm`). Si aucun ne convient :
   `-DOKFP_LIBRARY=<chemin de libokFrontPanel.so>`.
3. Accès USB sans root : installer la règle udev fournie avec le SDK (fichier `60-opalkelly.rules` ; à confirmer dans votre dossier), puis
   `sudo udevadm control --reload && sudo udevadm trigger`, rebrancher la carte.

## Compilation

```bash
cd opalkelly-opensourceCamera/Software/Cxx
cmake -S . -B build -DBUILD_GUI=ON -DOKFP_SDK=$HOME/FrontPanel-DevKit-6.1.0/API
cmake --build build -j4
```
Détection d'objets (facultatif) : archive `onnxruntime-linux-x64-<v>.tgz` (NUC) ou `onnxruntime-linux-aarch64-<v>.tgz` (Pi) des releases
Microsoft, puis `-DONNXRUNTIME_ROOT=<dossier décompressé>` (voir `models/README.md`).

## Utilisation sur NUC / Raspberry Pi

- **Écran** : l'application est une interface wxGTK (X11 ou Wayland). Sans écran : VNC ou `ssh -X`.
- **USB** : la vidéo brute 1080p30 fait ~187 Mo/s, plus 36 Mo/s d'audio : prendre un port USB 3 (bleu) et un câble USB 3 de qualité. Sur
  Raspberry Pi 5 la marge est plus réduite que sur NUC : à tester (compteur « lost » de la fenêtre Microphones, « dropped » du
  panneau d'enregistrement).
- **Enregistrement vidéo** : le Raspberry Pi 5 n'a pas d'encodeur H.264 matériel (ffmpeg/x264 logiciel). Choisir « Picture size » 50 % et/ou
  « Frame rate » 1 sur 2, ou un NUC.
- **CPU** : sur un serveur x86 à 2,1 GHz, le décodage de l'audio coûte ~2 % d'un cœur, une carte de beamforming 7,6 ms, une détection YOLO
  14,7 ms ; compter 1,5 à 3 fois plus par cœur sur un Raspberry Pi 5 (estimation).
