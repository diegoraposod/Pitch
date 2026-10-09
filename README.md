# PITCH · Onsen Estelar

Este repositorio tiene dos cosas:
- **index.html**: la app web (se abre con enlace https desde GitHub Pages, así el micrófono funciona también en el iPhone).
- **El plugin VST3 + Audio Unit** (carpeta Source), que GitHub compila solo para Mac.

## Plugin (VST3 + Audio Unit)

Plugin de efecto para Ableton (y cualquier DAW). Lo pones sobre una pista de audio y procesa lo que recibe: tu micrófono (pista en **In**), un clip, o nada (solo los drones).

## Qué hace
- **El mapa:** arrastras tu núcleo perlado hacia las piedras ámbar. La distancia decide el volumen, el filtro (200 Hz lejos → 8.2 kHz cerca), la posición estéreo y cuánta reverb.
- **Piedras:** Earth (Schumann 7.83 Hz × 16 = 125.28 Hz, con deriva natural y reacción al índice Kp real de la NOAA), White + Binaural, Granular (resonadores que tu señal hace sonar) y Pure Sine.
- **Fósiles:** suelta un archivo de audio (WAV, AIFF, FLAC, MP3…) en la zona de abajo o en cualquier parte del mapa, y nace un fósil que puedes visitar.
- **Grabar:** botón rojo arriba a la derecha. Graba la señal que entra a PITCH (hasta 30 s); al parar, se vuelve fósil y se guarda como WAV en `Música/PITCH/Fossils`.
- **Automatizable:** Core X, Core Y, Key, Scale, niveles de cada piedra, Fossils, Input, Space, Output, Earth Live.

## Controles
| Gesto | Acción |
|---|---|
| Arrastrar el núcleo o el agua | moverte |
| Tocar una piedra o el agua | flotar hasta ahí |
| Arrastrar una piedra o fósil arriba/abajo, o rueda sobre ella | su nivel |
| Doble clic en el agua, o rueda hacia abajo | volver a Earth |
| Clic en la nota o el modo (arriba a la izquierda) | cambiar tonalidad (clic derecho: hacia atrás) |
| Clic derecho en un fósil | quitarlo |

## Conseguir el plugin para Mac (sin instalar nada)
1. Crea un repositorio nuevo en GitHub y sube esta carpeta (sin `build/` ni `JUCE/`).
2. GitHub compila solo (pestaña **Actions** → "Build PITCH for Mac"). Tarda unos 10 minutos.
3. Al terminar, descarga **PITCH-mac** (abajo, en "Artifacts"), descomprímelo y haz doble clic en **install.command**.
   Si macOS no lo deja abrir: clic derecho → Abrir.
4. En Ableton: Preferencias → Plug-Ins → activa Audio Units / VST3 y pulsa **Rescan**.

## Compilar en tu Mac (si tienes Xcode)
```bash
cmake -B build -G Xcode
cmake --build build --config Release
```
Los plugins quedan en `build/PITCH_artefacts/Release/AU` y `.../VST3`. Luego ejecuta `install.command` desde esa carpeta (cópialo junto a los dos plugins).

## Nota
El plugin no está firmado por Apple (eso requiere una cuenta de desarrollador de 99 USD/año). Por eso el instalador le quita la "cuarentena" a los archivos. Para uso propio no hace falta más.
