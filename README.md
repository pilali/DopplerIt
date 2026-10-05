# DopplerIt

![DopplerIt modgui](dopplerit.lv2/modgui/screenshot-dopplerit.png)

Plug-in audio d'effet Doppler :
- **LV2** optimisé pour les **MOD Dwarf / Duo / Duo X** et le **Raspberry Pi 5**, avec un **modgui** complet pour mod-ui ;
- **VST3** (Windows, Linux, macOS) et **AU** (macOS Intel + Apple Silicon) via **JUCE**, avec une interface identique au modgui.

Les deux versions partagent exactement le même moteur DSP (`src/doppler_engine.hpp`) et les mêmes paramètres.

Une source sonore virtuelle se déplace en ligne droite devant l'auditeur, comme la
sirène d'un véhicule d'urgence qui passe : la hauteur monte quand elle s'approche,
puis descend quand elle s'éloigne.

- **1 entrée → 2 sorties**, avec un mode de sortie **Mono** ou **Stereo** dans le même plug-in.
- Modes **Approach** (approche), **Recede** (éloignement) ou **Pass-by** (les deux).
- Modèle physique exact (temps de propagation en forme fermée, rapport `c / (c − v·cosθ)`),
  pas une simple approximation par LFO.
- Atténuation de distance (loi en 1/r) et absorption de l'air (filtre passe-bas) réglables.
- Fondus enchaînés entre les passages : aucun clic au rebouclage, au déclenchement ou
  lors d'un changement de mode.
- Faible charge CPU : environ 0,5 % d'un cœur x86 en stéréo, une seule « oreille »
  calculée en mode mono.

## Contrôles

| Contrôle      | Plage            | Rôle |
|---------------|------------------|------|
| Mode          | Approach / Recede / Pass-by | Trajectoire de la source |
| Output        | Mono / Stereo    | Mono : le même signal sur les deux sorties (utilisez-en une seule dans une chaîne mono). Stereo : effet décalé entre gauche et droite |
| Speed         | 5 – 300 km/h     | Vitesse de la source : plus elle est rapide, plus le décalage de hauteur est fort (±1,5 demi-ton à 100 km/h) |
| Period        | 0,5 – 10 s       | Durée d'un passage, donc la cadence de répétition en boucle |
| Distance      | 1 – 50 m         | Distance minimale à l'auditeur : courte = bascule de hauteur brutale, longue = glissando doux |
| Attenuation   | 0 – 100 %        | Baisse de volume et perte d'aigus quand la source est loin |
| Width         | 0 – 100 %        | Stéréo uniquement : décalage de l'effet entre les oreilles et panoramique de la source |
| Dry/Wet       | 0 – 100 %        | Balance son direct / effet (les deux à plein niveau à 50 %) |
| Loop          | on / off         | On : passages en continu. Off : un passage par déclenchement |
| Pass          | bouton           | Relance un passage depuis le début (à assigner à un footswitch) |
| Bypass        | footswitch       | Désignation `lv2:enabled`, avec un fondu sans clic |

Astuce : avec **Loop = off** et **Pass** assigné à un footswitch du Dwarf, chaque
appui déclenche un passage de « sirène ».

## Compilation

Dépendances : `make`, `g++` et les en-têtes LV2 (`lv2-dev`).

### Directement sur un Raspberry Pi 5

```sh
sudo apt install build-essential lv2-dev
make                 # optimisé pour le CPU de la machine (Cortex-A76 sur Pi 5)
make install-user    # installe dans ~/.lv2
# ou : sudo make install   (installe dans /usr/local/lib/lv2)
```

Le plug-in est alors utilisable dans tout hôte LV2 (MODEP / mod-ui, Carla, Ardour,
jalv, etc.).

### Pour les MOD (Dwarf, Duo, Duo X) avec mod-plugin-builder

```sh
cp -r mod-plugin-builder/dopplerit /path/to/mod-plugin-builder/plugins/package/
cd /path/to/mod-plugin-builder
./build moddwarf dopplerit           # ou modduo, modduox
# depuis une copie locale plutôt que GitHub :
DOPPLERIT_SOURCE_DIR=/path/to/DopplerIt ./build moddwarf dopplerit
# puis pour envoyer le plug-in sur l'appareil connecté en USB :
./build moddwarf dopplerit-publish
```

La recette passe `NOOPT=true` : ce sont les options CPU de mod-plugin-builder qui
s'appliquent (Cortex-A53 pour le Dwarf et le Duo X, Cortex-A7 pour le Duo).

### Version JUCE : VST3 (Windows / Linux / macOS) et AU (macOS)

Dépendances : CMake ≥ 3.22 et un compilateur C++17 :
- Windows : Visual Studio 2022 ;
- macOS : Xcode ;
- Linux : `g++` plus les paquets X11, ALSA et freetype (voir `.github/workflows/build.yml`).

JUCE 8 est téléchargé automatiquement ; on peut aussi indiquer une copie locale avec `-DJUCE_DIR=/chemin/JUCE`.

```sh
# Windows / Linux
cmake -S juce -B build-juce -DCMAKE_BUILD_TYPE=Release
cmake --build build-juce --config Release

# macOS, binaire universel Intel + Apple Silicon (VST3 + AU)
cmake -S juce -B build-juce -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build-juce --config Release
```

Les plug-ins sont générés dans `build-juce/DopplerIt_artefacts/Release/` (`VST3/DopplerIt.vst3`, `AU/DopplerIt.component`).
Ajoutez `-DDOPPLERIT_STANDALONE=ON` pour obtenir aussi une application autonome.

- **macOS minimum** : 10.11 (El Capitan) pour la partie Intel, le minimum de JUCE 8. La partie Apple Silicon démarre à macOS 11, première version pour ces Mac.
- **Entrées et sorties** : entrée mono ou stéréo (sommée en mono) ; sortie mono ou stéréo. Avec une piste de sortie mono, l'effet est automatiquement l'effet mono simple.
- **Bypass** : le footswitch est le paramètre de bypass natif de l'hôte, avec un fondu sans clic.
- **Interface** :
  - même disposition 320 × 476, mêmes couleurs et mêmes images que le modgui (filmstrip du potard, footswitch, en-tête gyrophare) ;
  - même police, Cooper Hewitt : c'est celle que mod-ui impose à tous les modgui ;
  - la fenêtre est redimensionnable de 75 % à 300 %.
- **CI GitHub Actions** : chaque push compile Linux, Windows et macOS universel, valide le VST3 avec pluginval (sévérité 10), valide l'AU avec `auval` en natif et sous Rosetta, puis publie les binaires en artefacts. Un tag `v*` crée une release.
- **Signature macOS** : les binaires de la CI sont signés ad hoc. Pour les distribuer sans avertissement de Gatekeeper, il faut une signature Developer ID et une notarisation Apple. En attendant : `xattr -dr com.apple.quarantine DopplerIt.vst3`.

> **Licence JUCE** : JUCE 8 est distribué sous AGPLv3 ou sous licence commerciale JUCE (une offre gratuite existe en dessous d'un certain chiffre d'affaires). Distribuer les binaires VST3/AU impose soit de respecter l'AGPLv3, soit de disposer d'une licence JUCE. Le code de DopplerIt reste sous MIT, et la version LV2 n'utilise pas JUCE.

### Tests

```sh
make test
```

Les tests vérifient :
- la forme fermée du temps de propagation ;
- la hauteur mesurée par rapport à la théorie (`c/(c−v)` et `c/(c+v)`, à 0,4 % près) ;
- les modes de trajectoire ;
- l'absence de discontinuité lors des boucles, déclenchements, changements de mode,
  de paramètres et de sortie Mono/Stereo ;
- en mode mono, que L et R sont identiques ;
- le bypass ;
- la mémoire allouée et la charge CPU.

## Structure

```
src/doppler_engine.hpp      moteur DSP (sans dépendance)
src/dopplerit.cpp           enveloppe LV2
juce/                       version JUCE (VST3 / AU) : CMake, processeur, éditeur
.github/workflows/build.yml CI : LV2 + tests, VST3 Linux/Windows/macOS, AU macOS, pluginval, auval
dopplerit.lv2/              manifest, description des ports, modgui
tests/test_engine.cpp       tests hors ligne du moteur
tools/make_gui_images.py    génère le potard (filmstrip) et le footswitch
tools/make_screenshots.mjs  capture screenshot et thumbnail depuis un mod-ui en mode dev
mod-plugin-builder/         recette buildroot pour les appareils MOD
```

### modgui

Le template suit les conventions de mod-ui :
- jacks d'entrée et de sortie générés par mod-ui (`mod-role="input-audio-port"` /
  `"output-audio-port"`, avec les classes `mod-pedal-input-image` et `mod-pedal-output-image`) ;
- CSS espacé par `{{{cns}}}` et ressources chargées via `{{{ns}}}` ;
- widgets `film`, `custom-select` et `switch` ;
- footswitch `mod-role="bypass"`.

Le visuel s'inspire du gyrophare, l'exemple d'effet Doppler le plus familier. La LED
de bypass est un mini-gyrophare qui clignote en bleu et rouge quand l'effet est actif.

Le modgui a été validé dans un vrai mod-ui (mode développement) : le plug-in se
charge sans erreur JavaScript, les jacks sont rendus et tous les contrôles envoient
bien leurs valeurs.

## Licence

MIT, voir [LICENSE](LICENSE). La police Cooper Hewitt embarquée dans la version JUCE est sous SIL OFL 1.1 (`juce/Assets/fonts/OFL.txt`).
