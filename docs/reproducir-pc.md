# Reproducir en PC (Linux x86_64) el build release de dream-recomp con Ikaruga

Con tu propio disco de Ikaruga (Japan, T38706M) se obtiene el ejecutable recompilado que sirve de
herramienta de depuración (hash de escrituras) y el C++ generado que compila la PSP. Nada de esto se
sube a git: ni el disco, ni lo extraído, ni el C++ generado, ni las capturas.

Los comandos usan `$REPO` (la raíz de este repo) y se corren en un clon de dream-recomp, que en
`docs/primer-uso.md` vive en `build/dream-recomp`.

## 0. Requisitos

Linux x86_64, GCC 16.2, CMake 4.4, Python 3.14, SDL3 3.4, Vulkan 1.4 con `glslc` y una GPU con driver
Vulkan; son las versiones con las que se probó (otras más viejas deberían servir). No hace falta
`chdman` ni libchdr del sistema: la libchdr la compila el propio repo.

## 1. Fuente fijada

| Pieza | Valor |
|---|---|
| Repo | `https://github.com/phobos665/dream-recomp` |
| Commit | `51ca2766d2ebeae9caacda4e305eacb88cf64013` (`main`) |
| Submódulo | `third_party/libchdr` → `https://github.com/rtissera/libchdr` |

```sh
git clone https://github.com/phobos665/dream-recomp
cd dream-recomp
git checkout 51ca2766d2ebeae9caacda4e305eacb88cf64013
git submodule update --init --recursive     # sin esto, `dcdisc doctor` marca libchdr como MISS
```

## 2. El disco

El `ikaruga.toml` espera el CHD en `games/ikaruga/disc/Ikaruga (Japan).chd` (ruta que ignora el
`.gitignore` de dream-recomp). CHD de referencia: 26 841 157 bytes, sha256
`a322d7ee917014a006c18b296ad82f12bce558ca4d26cf9954c6b8096f3a9a66`. Otro volcado correcto puede dar un
CHD distinto; lo que tiene que coincidir es el sha1 de `1ST_READ.BIN` del paso 4.

```sh
mkdir -p juego games/ikaruga/disc
cp /ruta/a/tu/Ikaruga.chd juego/Ikaruga.chd
ln -sf ../../../juego/Ikaruga.chd "games/ikaruga/disc/Ikaruga (Japan).chd"
```

## 3. Primer configure y build (compila libchdr)

```sh
cmake -S . -B build-rel -DDREAM_DEV_INTERPRETER=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel --parallel "$(nproc)"
```

`DREAM_DEV_INTERPRETER=OFF` = release sin intérprete SH-4 de respaldo. El target `ikaruga_boot` todavía
no existe: aparece cuando ya hay `games/ikaruga/extracted/` (paso 5).

## 4. Extraer el disco con dcdisc (sin `pip install`)

```sh
export PYTHONPATH=$PWD/tools/dcdisc
export DCDISC_LIBCHDR=$PWD/build-rel/third_party/libchdr/libchdr.so
python3 -m dcdisc extract "games/ikaruga/disc/Ikaruga (Japan).chd" games/ikaruga/extracted
sha1sum games/ikaruga/extracted/fs/1ST_READ.BIN
```

Esperado: la salida `wrote IP.BIN and 737 files`, un `1ST_READ.BIN` de 1 027 892 bytes y
`f2524eb571da1b0fb4a9b2cdab448ce24764c6f9`. Si el sha1 no coincide, para aquí: el resto de la
configuración (`ikaruga.toml`, direcciones, semillas) está hecho para ese ejecutable.

## 5. Generar y compilar el juego

Antes de este paso aplica los parches de este repo (5.1): el EBOOT de la PSP necesita el C++ generado con
idle-skip.

```sh
cmake -S . -B build-rel                       # reconfigura: ahora aparece ikaruga_boot
cmake --build build-rel --target ikaruga_boot --parallel "$(nproc)"
size build-rel/games/ikaruga/ikaruga_boot
```

El C++ generado queda en `build-rel/games/ikaruga/gen/` (20 archivos, 48 MB). El binario depende de
`libvulkan.so.1`, `libSDL3.so.0` y la libc/libstdc++; compilar tarda ≈1 min 20 s con 24 hilos.

El C++ generado es determinista: con el mismo commit de dream-recomp y el mismo `1ST_READ.BIN` sale
idéntico. Se comprueba con

```sh
(cd build-rel/games/ikaruga/gen && cat $(ls | sort) | sha256sum)
```

| árbol | sha256 esperado |
|---|---|
| dream-recomp sin parches | `095e71eaeafdd6444586606cff3f40992541c705ab7eeb7e76eb7ffa9c94333a` |
| con `patches/dream-recomp-pc/*.patch` (5.1) | `924f8b67d9778173fba9dbcd15117eb36f84aa51ca355b6158837860aec884da` |

Si el hash no sale tras reaplicar los parches sobre un `build-rel` existente, borra
`build-rel/games/ikaruga/gen` y repite.

### 5.1 Parches de este repo (`patches/dream-recomp-pc/`)

```sh
git apply $REPO/patches/dream-recomp-pc/*.patch
git diff --stat      # docs, games/ikaruga/ikaruga.toml, runtime/, translator/
```

- `0001`: idle-skip. Con la tabla `[idle]` del `ikaruga.toml` el traductor emite `idle_poll` en la cabeza
  de la espera de `kmWaitVBlank` (`docs/idle-skip.md`). Cambia el `gen/` (hash de arriba).
- `0002`: FMAC, FIPR y FTRV en `float` simple, el mismo código que la PSP
  (`patches/dream-recomp/0009-fpu-float.patch`; `docs/velocidad-psp.md`). Solo toca
  `runtime/include/dream/runtime/sh4/ops.h`: el `gen/` no cambia, el binario sí
  (`ikaruga_boot` de 14 402 032 B).

Con los dos parches `ikaruga_boot` mide 14 402 032 B (el de 0001 solo: 14 430 776 B, text 13 152 407,
data 227 080, bss 590 520; sin parches, 14 425 888 B).

Para comparar bit a bit contra el EBOOT `DREAM_FPU_DOUBLE=1` (`docs/correctitud-psp.md`), un segundo
build del mismo árbol:

```sh
cmake -S . -B build-dbl -DDREAM_DEV_INTERPRETER=OFF -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS=-DDREAM_FPU_DOUBLE=1
cmake --build build-dbl --target ikaruga_boot --parallel "$(nproc)"   # 14 430 776 B
```

Los dos lados (PC y PSP) compilan con `-ffp-contract=off -frounding-math` (`cmake/DreamTargetDefaults.cmake`
en dream-recomp, `psp/ikaruga/Makefile` aquí): sin FMA implícita.

`scripts/build-psp-ikaruga.sh` aplica su propia copia de la parte del runtime
(`patches/dream-recomp/*.patch`) sobre el submódulo; del PC solo necesita el `gen/` regenerado.

## 6. Correr

Opciones del lanzador usadas:

| opción | qué hace |
|---|---|
| `--config games/ikaruga/ikaruga.toml` | configuración del juego |
| `--vmu archivo.vmu` | tarjeta de memoria (se crea si no existe) |
| `--window` | ventana Vulkan 640×480 con vsync; sin ella corre headless sin límite de velocidad |
| `--press boton@frame,...` | botones simulados en cuadros dados |
| `--screenshot-at N` | guarda `screenshot-000.ppm` en el directorio actual en el cuadro N |
| `--max-seconds S` / `--max-frames N` | se detiene a los S segundos de juego / en el cuadro N |
| `--report archivo` | escribe el reporte final (contadores, `untranslated call targets`) |
| `--rtc-seed 1` | reloj fijo, para corridas repetibles |
| `--write-hash archivo` | `write_hash` de las escrituras del guest por cuadro (el oráculo de `scripts/test-psp-igual-pc.sh`) |
| `--fps` | contador en pantalla (no va al log) |
| `--no-audio` | sin dispositivo de audio |

### 6.1 Headless

```sh
build-rel/games/ikaruga/ikaruga_boot --config games/ikaruga/ikaruga.toml \
  --vmu juego/ikaruga.vmu --rtc-seed 1 --no-audio --max-seconds 60 \
  --report juego/report-headless.txt
grep 'untranslated call targets' juego/report-headless.txt
```

Imprime `host: N s (Mx real time)` con M ≈ 5 (60 s de juego en ≈12 s incluyendo gameplay). El reporte
debe decir `untranslated call targets: 0`.

### 6.2 Con ventana: título y capítulo 1

Las capturas salen en el directorio actual, por eso se corre desde `juego/`:

```sh
W=$PWD; cd juego
# Título: logo 斑鳩 / IKARUGA hacia el cuadro 600 (~10 s)
$W/build-rel/games/ikaruga/ikaruga_boot --config $W/games/ikaruga/ikaruga.toml \
  --vmu $W/juego/ikaruga.vmu --window --fps --rtc-seed 1 --max-seconds 15 \
  --screenshot-at 600 --report $W/juego/report-title.txt
mv screenshot-000.ppm titulo.ppm
# Capítulo 1: START/A simulados, captura en el cuadro 2600 (~43 s)
$W/build-rel/games/ikaruga/ikaruga_boot --config $W/games/ikaruga/ikaruga.toml \
  --vmu $W/juego/ikaruga.vmu --window --fps --rtc-seed 1 --max-seconds 50 \
  --press start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200 \
  --screenshot-at 2600 --report $W/juego/report-cap1.txt
mv screenshot-000.ppm capitulo1.ppm
cd $W
```

Qué se debe ver:

- `titulo.ppm`: fondo negro con el logo 斑鳩 / IKARUGA abajo a la derecha.
- `capitulo1.ppm`: gameplay del capítulo 1: nave blanca entre estructuras, HUD con `1P`, medidor `CHAIN`
  y vidas.
- Con `--max-seconds 120` y `--screenshot-at 6500` llega a la pantalla `PLAY DATA / 1-PLAYER`
  (capítulo 1 = 1 500 puntos): la nave sin control pierde las vidas.
- ≈60 fps en ventana; `untranslated call targets: 0`.

Controles a mano: flechas, Z/X/A/S, Enter.

## 7. Diferencias conocidas

- Sin `--bios` (volcado propio de la boot ROM) los textos de la tarjeta de memoria salen en blanco; el
  juego corre igual (`games/ikaruga/README.md` de dream-recomp).
- 6 accesos a memoria sin mapear en el cuadro 0, inocuos.
- Sin verificar: jugar a mano, capítulos 2–5, audio a oído.
- Este ejecutable es herramienta de depuración (`write_hash`): la imagen del port se juzga contra Flycast
  (`docs/referencia-flycast.md`), no contra lo que dibuja el PC.
