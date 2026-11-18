# Modèle de détection d'objets (application C++)

`yolo26n_320.onnx` : YOLO26n (Ultralytics), entraîné sur COCO (80 classes), entrée 320 x 320, **NMS intégré**
(sortie `[1, 300, 6]` = x1, y1, x2, y2, score, classe). C'est le même réseau que `FP-Platform/assets/models/yolo26n_*`
(application TypeScript), exporté en ONNX. Licence des poids : AGPL-3.0 (Ultralytics).

Export (Python, `pip install ultralytics onnx onnxslim`) :

```python
from ultralytics import YOLO
YOLO("yolo26n.pt").export(format="onnx", imgsz=320, simplify=True, opset=17, end2end=True)
```

Un export 640 x 640 est plus précis et ~4x plus lent : remplacer le fichier (ou `OKCAM_YOLO_MODEL=<chemin>`), la taille d'entrée
est lue dans le modèle. Un export SANS `end2end=True` (sortie `[1, 84, 2100]`) est refusé avec un message explicite.

## ONNX Runtime (nécessaire pour compiler la détection)

Sans lui l'application se compile et fonctionne, la case « Detect objects » est simplement grisée.

* Windows : paquet NuGet `Microsoft.ML.OnnxRuntime` (renommer `.nupkg` en `.zip` et décompresser), ou l'archive
  `onnxruntime-win-x64-<version>.zip` des releases GitHub de Microsoft.
* Linux / macOS : archive `onnxruntime-<os>-<arch>-<version>.tgz` des releases GitHub.

```bash
cmake -S . -B build -DBUILD_GUI=ON -DONNXRUNTIME_ROOT=<dossier décompressé>
```

CMake trouve `include/` + `lib/` (archive) ou `build/native/include` + `runtimes/<rid>/native` (NuGet), copie la DLL / `.so` à
côté de l'exécutable et le modèle dans `models/`. Testé avec ONNX Runtime 1.22 (Linux x64).

Vérification sans l'interface : `tests/onnx_detect_check.cpp` (image RGB brute -> boîtes, à comparer avec Ultralytics ; sur l'image
d'exemple `bus.jpg` les 5 boîtes coïncident à moins d'un pixel, ~14 ms par image sur un CPU de serveur).

## CPU utilisée

Par défaut : 1 thread d'inférence, threads inactifs endormis (pas d'attente active) et 5 analyses par seconde au plus (liste
« 2 / 5 / 10 / Max » à côté de « Detect objects »). Mesuré sur `bus.jpg` : 14,7 ms de CPU par analyse avec 1 thread, contre 20 ms avec
2 threads et 31 ms avec tous les cœurs, pour la même latence ; à 5 / s cela fait ~7 % d'un cœur.
