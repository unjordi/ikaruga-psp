# Primer uso: de tu disco a la carpeta de la PSP

Receta corta, probada de punta a punta desde un worktree limpio de `develop` (`6b98ee9`) el
2026-11-06, sin builds previos (solo la imagen Docker del toolchain ya existía; se reconstruyó
también desde cero, abajo). El detalle de cada paso está en `docs/reproducir-pc.md` y
`docs/correctitud-psp.md`. Nada del juego va al repo: todo vive en `build/` (gitignored).

**Estado real:** en PPSSPP el EBOOT corre igual que PC, escritura por escritura, en los cuadros
60…600 y dibuja la pantalla de título con texturas. **No es jugable todavía y no se ha probado en
una PSP física.**

## Requisitos

- Linux x86_64 con GCC, CMake, Python 3, SDL3, Vulkan con `glslc` (probado: GCC 16.2, CMake 4.4,
  Python 3.14, SDL3 3.4, Vulkan 1.4). Solo para el binario de PC y el C++ generado.
- Docker (el toolchain PSPDEV va en la imagen `scripts/psp/Dockerfile`; el script la construye si
  no existe, ~1 min).
- Para las pruebas: flatpak `org.ppsspp.PPSSPP` (probado 1.20.4), `xvfb-run`, ImageMagick
  (`import`), `genisoimage`.
- Tu disco de Ikaruga (Japan, T38706M) en CHD.

## Comandos, en orden (desde la raíz del repo)

```sh
git submodule update --init third_party/dream-recomp   # build-psp-ikaruga.sh lo necesita
mkdir -p build && cd build

# 1. dream-recomp fijado + parche de idle-skip (docs/reproducir-pc.md §1, §5.2)
git clone https://github.com/phobos665/dream-recomp && cd dream-recomp
git checkout 51ca2766d2ebeae9caacda4e305eacb88cf64013
git submodule update --init --recursive
git apply ../../patches/dream-recomp-pc/*.patch

# 2. el disco (copia, el original no se toca)
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
(cd build-rel/games/ikaruga/gen && cat $(ls | sort) | sha256sum)   # 924f8b67…
cd ../..

# 6. EBOOT de PSP: se construye desde el submódulo (DREAM_RECOMP sin definir); el dream-recomp de
#    build/ ya trae el parche de PC y build-psp-ikaruga.sh volvería a parcharlo
scripts/build-psp-ikaruga.sh build/dream-recomp/build-rel/games/ikaruga/gen --variantes=mix

# DREAM_RECOMP desde aquí: el binario de PC para las pruebas y volcar-area-hd
export DREAM_RECOMP=$PWD/build/dream-recomp

# 7. el disco real por área HD (NO scripts/preparar-disco-ikaruga.sh: ese ISO diverge de PC)
D=$DREAM_RECOMP; L=$(find $D/build-rel/third_party -name '*.a' | tr '\n' ' ')
mkdir -p build/tools
g++ -std=gnu++20 -O2 -I$D/runtime/include tools/volcar-area-hd.cpp \
  $D/build-rel/runtime/libdream_runtime.a $L $L -lzstd -o build/tools/volcar-area-hd
build/tools/volcar-area-hd "$D/games/ikaruga/disc/Ikaruga (Japan).chd" build/DISCO-real.ISO

# 8. la carpeta de la memory stick
M=build/memstick/PSP/GAME/IKARUGA; mkdir -p $M
cp build/psp/ikaruga/EBOOT.PBP $D/games/ikaruga/extracted/fs/1ST_READ.BIN $M/
cp build/DISCO-real.ISO $M/DISCO.ISO

# 9. pruebas en PPSSPP (una a la vez; ~6 min cada una)
export PROBAR_DISCO=$PWD/$M/DISCO.ISO
scripts/test-psp-igual-pc.sh $M/EBOOT.PBP $D/games/ikaruga/extracted
scripts/test-psp-titulo.sh $M/EBOOT.PBP $D/games/ikaruga/extracted \
  $D/build-rel/games/ikaruga/ikaruga_boot $D/games/ikaruga/ikaruga.toml
```

## Qué copiar a la memory stick

El contenido de `build/memstick/PSP/GAME/IKARUGA/` a `ms0:/PSP/GAME/IKARUGA/` (el nombre de la
carpeta puede ser otro: el EBOOT la toma de `argv[0]`):

| archivo | tamaño |
|---|---|
| `EBOOT.PBP` | 13 885 660 B |
| `1ST_READ.BIN` | 1 027 892 B |
| `DISCO.ISO` | 1 032 499 200 B |
| **total** | **1 047 412 752 B (~999 MiB)** |

Cabe en FAT32 (ningún archivo pasa de 4 GiB). El EBOOT crea `VMU.BIN` (la tarjeta de memoria,
128 KiB) y `ikaruga.log` en la misma carpeta; `ikaruga.cfg` es opcional.

## Qué debe verse

Las pruebas imprimen `ok: PC and PSP write_hash equal at 10 frames (60..600)` y `(a) ok`,
`(b) ok`, `(c) ok`. En pantalla (PPSSPP), hacia el cuadro ~1243 de guest, la pantalla de título
con texturas: arte de la nave en tonos naranja y azul, el logo 斑鳩 / IKARUGA en el centro,
`PRESS START BUTTON`, `DIFFICULTY/USER SELECT`, las líneas `EXTEND`/`EVERY`/`PLAYER STOCK` y
`©TREASURE/ESP 2001,2002` (comprobado a ojo, a 480×272, sobre
`build/psp/evidencia/titulo-1243/psp-1243.png`). No hay jugabilidad todavía.

## Hashes de la corrida (2026-11-06)

| pieza | valor |
|---|---|
| CHD de entrada | 26 841 157 B, sha256 `a322d7ee917014a006c18b296ad82f12bce558ca4d26cf9954c6b8096f3a9a66` |
| `1ST_READ.BIN` | sha1 `f2524eb571da1b0fb4a9b2cdab448ce24764c6f9` (sha256 `b3c9a2b98a4893a9de9436618c4bdeea3edaa9d774714ff05f8f47942ed855de`); extracción: `wrote IP.BIN and 737 files` |
| `gen/` con idle-skip | 20 archivos, 48 MB, `924f8b67d9778173fba9dbcd15117eb36f84aa51ca355b6158837860aec884da` (= doc) |
| `ikaruga_boot` (PC, parchado) | 14 430 776 B; text 13 152 407, data 227 080, bss 590 520 |
| `EBOOT.PBP` (mix) | 13 885 660 B, sha256 `d9840d5c6ba31bbf89a1be5c72b2d775544cfc534ce424615179647c67d7523c` (= develop) — igual con la imagen cacheada y con una reconstruida `--no-cache` |
| `DISCO.ISO` (área HD real) | 504 150 sectores, 0 ilegibles, 1 032 499 200 B, sha256 `c3173ebddd1058894b3b806e2ced63ededa2a66efdc15f9467585d388e110db7` |
| `test-psp-igual-pc.sh` | PASS, 10/10 cuadros 60…600 (cuadro 60 `b003d78fd04385d4`, 600 `2986fff765029c09`) |
| `test-psp-titulo.sh` | PASS: (a) 28.43 dB, (b) 9.46 dB sin texturas, (c) 7.37 dB contra cuadro 800 |

## Hallazgos de la corrida (y qué se corrigió)

1. `docs/arranque-psp.md` decía que el `DISCO.ISO` se hace con `scripts/preparar-disco-ikaruga.sh`;
   ese ISO no es el disco real y diverge de PC en el cuadro 4. Corregido para apuntar a
   `tools/volcar-area-hd.cpp`.
2. La tabla de tamaños de `docs/reproducir-pc.md` §5 es del binario sin parche; con el parche de
   §5.2 sale otro tamaño. Añadido el valor medido.
3. Ningún doc decía, en el camino de un usuario nuevo, que el submódulo `third_party/dream-recomp`
   de este repo hay que inicializarlo antes de `build-psp-ikaruga.sh` (el script sí lo avisa al
   fallar). Queda en esta receta.
4. La compilación de `volcar-area-hd` solo estaba como bloque en `docs/correctitud-psp.md`; funciona
   tal cual. Queda aquí también.
5. `scripts/build-psp-ikaruga.sh` acepta ahora `IKARUGA_PSP_IMAGE` para construir con otra etiqueta
   de imagen (así se comprobó que la imagen reconstruida da el mismo EBOOT).
6. El README todavía dice «nothing playable on PSP yet»: sigue siendo cierto (solo título).
