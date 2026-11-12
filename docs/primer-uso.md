# Primer uso: de tu disco a la carpeta de la PSP

Receta de punta a punta, desde un clon limpio de `develop`. El detalle del lado PC está en
`docs/reproducir-pc.md` y el del oráculo en `docs/correctitud-psp.md`. Nada del juego va al repo: todo
vive en `build/` (ignorado por git).

**Estado:** en PPSSPP el EBOOT corre el juego igual que el PC, escritura por escritura, en el arranque,
y llega al título, al menú de modos y al capítulo 1, a ≈0,086x del tiempo real (≈5 fps; la meta
jugable es ≈30 fps). Sin audio. No se ha probado en una PSP física.

## Requisitos

- Linux x86_64 con GCC, CMake, Python 3, SDL3 y Vulkan con `glslc` (probado con GCC 16.2, CMake 4.4,
  Python 3.14, SDL3 3.4, Vulkan 1.4). Solo para el binario de PC y el C++ generado.
- Docker: el toolchain PSPDEV va en la imagen `scripts/psp/Dockerfile`; el script la construye si no
  existe (~1 min).
- Para las pruebas: flatpak `org.ppsspp.PPSSPP` (probado 1.20.4), `xvfb-run`, `xdotool`, ImageMagick
  (`import`), `genisoimage`, y Flycast (flatpak `org.flycast.Flycast`, `ffmpeg`, Python con PIL) para las
  pruebas de imagen (`docs/referencia-flycast.md`).
- Tu disco de Ikaruga (Japan, T38706M) en CHD.

## Comandos, en orden (desde la raíz del repo)

```sh
git submodule update --init third_party/dream-recomp   # build-psp-ikaruga.sh lo necesita
mkdir -p build && cd build

# 1. dream-recomp fijado + parches del PC (docs/reproducir-pc.md §1 y §5.1)
git clone https://github.com/phobos665/dream-recomp && cd dream-recomp
git checkout 51ca2766d2ebeae9caacda4e305eacb88cf64013
git submodule update --init --recursive
git apply ../../patches/dream-recomp-pc/*.patch

# 2. el disco (copia; el original no se toca)
mkdir -p juego games/ikaruga/disc
cp /ruta/a/Ikaruga.chd juego/Ikaruga.chd
ln -sf ../../../juego/Ikaruga.chd "games/ikaruga/disc/Ikaruga (Japan).chd"

# 3-5. build, extracción, C++ generado y binario de PC
cmake -S . -B build-rel -DDREAM_DEV_INTERPRETER=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel --parallel "$(nproc)"
PYTHONPATH=$PWD/tools/dcdisc DCDISC_LIBCHDR=$PWD/build-rel/third_party/libchdr/libchdr.so \
  python3 -m dcdisc extract "games/ikaruga/disc/Ikaruga (Japan).chd" games/ikaruga/extracted
cmake -S . -B build-rel
cmake --build build-rel --target ikaruga_boot --parallel "$(nproc)"
(cd build-rel/games/ikaruga/gen && cat $(ls | sort) | sha256sum)   # 924f8b67… (valores de referencia, abajo)
cd ../..

# 6. EBOOT de PSP. DREAM_RECOMP sin definir: construye desde el submódulo, que el script parcha
#    con patches/dream-recomp/. Si cambias un parche que toque ops.h (va en línea en el C++
#    generado), agrega --limpio: el script reutiliza obj/gen y el EBOOT quedaría con el ops.h viejo.
scripts/build-psp-ikaruga.sh build/dream-recomp/build-rel/games/ikaruga/gen --variantes=mix

# DREAM_RECOMP desde aquí: el binario de PC para el oráculo de igualdad y volcar-area-hd
export DREAM_RECOMP=$PWD/build/dream-recomp

# 7. el disco real por área HD (no scripts/preparar-disco-ikaruga.sh: ese ISO diverge del PC)
D=$DREAM_RECOMP; L=$(find $D/build-rel/third_party -name '*.a' | tr '\n' ' ')
mkdir -p build/tools
g++ -std=gnu++20 -O2 -I$D/runtime/include tools/volcar-area-hd.cpp \
  $D/build-rel/runtime/libdream_runtime.a $L $L -lzstd -o build/tools/volcar-area-hd
build/tools/volcar-area-hd "$D/games/ikaruga/disc/Ikaruga (Japan).chd" build/DISCO-real.ISO

# 8. la carpeta de la memory stick
M=build/memstick/PSP/GAME/IKARUGA; mkdir -p $M
cp build/psp/ikaruga/EBOOT.PBP $D/games/ikaruga/extracted/fs/1ST_READ.BIN $M/
cp build/DISCO-real.ISO $M/DISCO.ISO

# 9. pruebas en PPSSPP (una a la vez)
export PROBAR_DISCO=$PWD/$M/DISCO.ISO
J=$D/games/ikaruga/extracted
HASTA=240 scripts/test-psp-igual-pc.sh $M/EBOOT.PBP $J     # arranque igual al PC, ~10 s
scripts/test-psp-titulo.sh $M/EBOOT.PBP $J                 # imagen contra Flycast; graba la serie la primera vez
```

## Qué copiar a la memory stick

El contenido de `build/memstick/PSP/GAME/IKARUGA/` a `ms0:/PSP/GAME/IKARUGA/` (la carpeta puede llamarse
distinto: el EBOOT la toma de `argv[0]`):

| archivo | tamaño |
|---|---|
| `EBOOT.PBP` | ≈14 MB |
| `1ST_READ.BIN` | 1 027 892 B |
| `DISCO.ISO` | 1 032 499 200 B |

Total ≈ 999 MiB; cabe en FAT32 (ningún archivo pasa de 4 GiB). El EBOOT crea `VMU.BIN` (la tarjeta
de memoria, 128 KiB) e `ikaruga.log` en la misma carpeta. `ikaruga.cfg` es opcional.

## `ikaruga.cfg`

Junto al EBOOT; una clave por línea, `clave = valor`, `#` comenta. Las claves de depuración y de las
pruebas son las que los demás docs citan.

| clave | efecto |
|---|---|
| `boton_cruz`, `boton_circulo`, `boton_cuadrado`, `boton_triangulo`, `boton_l`, `boton_r`, `boton_start`, `boton_select`, `umbral_stick`, `tate`, `control` | controles (`docs/controles-psp.md`) |
| `pulsar=start@700,…` | pulsaciones guionadas, sintaxis de `--press` del PC; cada una dura 8 cuadros |
| `rtc_seed=N` (alias `rtc_semilla`) | reloj de la consola en N segundos desde 1970 (1 por omisión) |
| `hash_every=N` | cuadros entre líneas `hash` (60 por omisión; 1 ubica el primer cuadro distinto del PC) |
| `log_from=I`, `log_count=N` | las escrituras del guest [I, I+N) van a `writes.log` (formato `--write-log` del PC) |
| `test_exit_at=N`, `test_suspend_at=N` | en el cuadro N notifica la salida (HOME) / simula suspender y reanudar |
| `capturar_en=N` | tras dibujar el primer render en o después del cuadro N escribe `captura-N.raw` (RGB888, 480×272) y sale |
| `max_cuadros=N` | sale en el cuadro N |
| `dibujar=0` / `dibujar_desde=N` | no dibuja geometría / no llama al video antes del cuadro N |
| `trazar=1`, `profundidad=0`, `volcar_ta=1` | traza cada tira del render capturado / sin prueba de profundidad / escribe `ta-N.bin` |
| `texturas=0`, `texturas_kb=N` | polígonos solo con color de vértice / tope de la caché de texturas en KB (2560) |
| `recorte=0`, `niebla=0` | ignora el recorte de usuario / dibuja sin niebla (`docs/video-psp.md`) |
| `memoria=1`, `memoria_cada=N` | contador de memoria (`docs/memoria-psp.md`) |
| `guardar_estado_en=N`, `cargar_estado=archivo` | estado guardado (`docs/estado-guardado.md`) |

## Qué debe verse

Las pruebas imprimen `ok: PC and PSP write_hash equal at 4 frames (60..240)` (o `10 frames (60..600)`
sin `HASTA`) y `(a) ok`, `(b) ok`, `(c) ok`. En pantalla (PPSSPP), hacia el cuadro 1243 de guest, el
título con texturas: arte de la nave en tonos naranja y azul, el logo 斑鳩 / IKARUGA en el centro,
`PRESS START BUTTON`, `DIFFICULTY/USER SELECT`, las líneas `EXTEND`/`EVERY`/`PLAYER STOCK` y
`©TREASURE/ESP 2001,2002`. Con las pulsaciones del capítulo 1 (`docs/referencia-flycast.md`) se llega al
menú de modos y al capítulo 1.

## Valores de referencia

Para comprobar que lo que armaste es lo mismo (el disco y lo derivado son del usuario; no están en el repo):

| pieza | valor |
|---|---|
| CHD de entrada | 26 841 157 B, sha256 `a322d7ee917014a006c18b296ad82f12bce558ca4d26cf9954c6b8096f3a9a66` |
| `1ST_READ.BIN` | 1 027 892 B, sha1 `f2524eb571da1b0fb4a9b2cdab448ce24764c6f9` (sha256 `b3c9a2b98a4893a9de9436618c4bdeea3edaa9d774714ff05f8f47942ed855de`); la extracción dice `wrote IP.BIN and 737 files` |
| `gen/` con idle-skip | 20 archivos, 48 MB, sha256 de la concatenación ordenada `924f8b67d9778173fba9dbcd15117eb36f84aa51ca355b6158837860aec884da` |
| `DISCO.ISO` (área HD real) | 504 150 sectores, 0 ilegibles, 1 032 499 200 B, sha256 `c3173ebddd1058894b3b806e2ced63ededa2a66efdc15f9467585d388e110db7` |
| cuadro 60 del `write_hash` (sin entrada, `rtc_seed 1`) | `b003d78fd04385d4` |
