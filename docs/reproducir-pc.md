# Reproducir en PC (Linux x86_64) el build release de dream-recomp con Ikaruga

Objetivo: con tu propio disco de Ikaruga (Japan, T38706M) obtener el mismo ejecutable recompilado
que usamos como referencia. Nada de esto se sube a git: ni el disco, ni lo extraído, ni el C++
generado, ni las capturas.

Etiquetas: **CONFIRMADO** (abierto o corrido, se dice dónde), **SEGÚN <fuente>**, **[INFER]**.

## 0. Requisitos

SEGÚN `~/code/ajenos/dr-rev/RESULTADO-IKARUGA.md` (host de la medición): Linux x86_64, GCC 16.2,
CMake 4.4, Python 3.14, SDL3 3.4, Vulkan 1.4 con `glslc`, GPU con driver Vulkan. No hizo falta
instalar `chdman` ni libchdr del sistema: la libchdr la compila el propio repo.

[INFER] Versiones algo más viejas de GCC/CMake deberían servir; solo esas están probadas.

## 1. Fuente fijada

| Pieza | Valor | Estado |
|---|---|---|
| Repo | `https://github.com/phobos665/dream-recomp` | CONFIRMADO: `config` del clon `~/code/ajenos/dream-recomp` |
| Commit | `51ca2766d2ebeae9caacda4e305eacb88cf64013` (`main`) | CONFIRMADO: `FETCH_HEAD`/`packed-refs` del clon; el árbol medido (`dr-rev`, commit `a0fa969`) es ese commit más un único archivo de notas |
| Submódulo | `third_party/libchdr` → `https://github.com/rtissera/libchdr` | CONFIRMADO: `.gitmodules` de `dr-rev` |

```sh
git clone https://github.com/phobos665/dream-recomp
cd dream-recomp
git checkout 51ca2766d2ebeae9caacda4e305eacb88cf64013
git submodule update --init --recursive     # sin esto, `dcdisc doctor` marca libchdr como MISS
```

## 2. El disco

El `ikaruga.toml` espera el CHD en `games/ikaruga/disc/Ikaruga (Japan).chd` (SEGÚN
`RESULTADO-IKARUGA.md`; esa ruta la ignora el `.gitignore` del repo). Referencia del CHD medido:
26 841 157 bytes, sha256 `a322d7ee917014a006c18b296ad82f12bce558ca4d26cf9954c6b8096f3a9a66`
(SEGÚN `RESULTADO-IKARUGA.md`). Otro volcado correcto puede dar un CHD distinto; lo que tiene que
coincidir es el sha1 de `1ST_READ.BIN` del paso 4.

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

`DREAM_DEV_INTERPRETER=OFF` = release sin intérprete SH-4 de respaldo (SEGÚN
`RESULTADO-IKARUGA.md`). El target `ikaruga_boot` todavía no existe: aparece cuando ya hay
`games/ikaruga/extracted/` (paso 5).

## 4. Extraer el disco con dcdisc (sin `pip install`)

```sh
export PYTHONPATH=$PWD/tools/dcdisc
export DCDISC_LIBCHDR=$PWD/build-rel/third_party/libchdr/libchdr.so
python3 -m dcdisc extract "games/ikaruga/disc/Ikaruga (Japan).chd" games/ikaruga/extracted
sha1sum games/ikaruga/extracted/fs/1ST_READ.BIN
```

Esperado:

- salida `wrote IP.BIN and 737 files`; `1ST_READ.BIN` de 1 027 892 bytes (SEGÚN `RESULTADO-IKARUGA.md`).
- `f2524eb571da1b0fb4a9b2cdab448ce24764c6f9  .../1ST_READ.BIN` — CONFIRMADO: `sha1sum` sobre
  `~/code/ajenos/dr-rev/games/ikaruga/extracted/fs/1ST_READ.BIN`.

Si el sha1 no coincide, para aquí: el resto de la configuración (`ikaruga.toml`, direcciones,
semillas) está hecho para ese ejecutable.

La medición original apuntó `DCDISC_LIBCHDR` a `build-dev/...` (build con intérprete); usar la
de `build-rel` es [INFER] equivalente, porque la libchdr no depende de esa opción.

## 5. Generar y compilar el juego

```sh
cmake -S . -B build-rel                       # reconfigura: ahora aparece ikaruga_boot
cmake --build build-rel --target ikaruga_boot --parallel "$(nproc)"
size build-rel/games/ikaruga/ikaruga_boot
```

Referencia, CONFIRMADO con `ls -l` y `size` sobre `~/code/ajenos/dr-rev/build-rel/games/ikaruga/ikaruga_boot`:

| archivo | text | data | bss |
|---|---|---|---|
| 14 425 888 B | 13 146 938 | 227 088 | 590 184 |

El C++ generado queda en `build-rel/games/ikaruga/gen/` (20 archivos, 48 MB; CONFIRMADO con `ls`
y `du`). Dependencias dinámicas: `libvulkan.so.1`, `libSDL3.so.0` y la libc/libstdc++ (SEGÚN
`RESULTADO-IKARUGA.md`). Compilar tarda ~1 min 19 s de pared con 24 hilos (misma fuente).

### 5.1 Comprobar que generaste lo mismo

El C++ generado es determinista: con el mismo commit de dream-recomp y el mismo `1ST_READ.BIN`
sale idéntico. Compáralo así:

```sh
(cd build-rel/games/ikaruga/gen && cat $(ls | sort) | sha256sum)
```

Esperado: `095e71eaeafdd6444586606cff3f40992541c705ab7eeb7e76eb7ffa9c94333a`. CONFIRMADO: borrando `gen/` y regenerando sale el mismo hash.

### 5.2 Con idle-skip (`patches/dream-recomp-pc/`)

El EBOOT de la PSP con idle-skip (docs/arranque-psp.md, «Idle-skip en la PSP») usa el C++ que
genera el traductor parchado: con la tabla `[idle]` del `ikaruga.toml` emite `idle_poll` en la
cabeza de la espera de `kmWaitVBlank` (docs/idle-skip.md). Antes del paso 3 (o en un clon aparte),
aplica los parches de este repo sobre el commit fijado:

```sh
git apply /ruta/a/ikaruga-psp/patches/dream-recomp-pc/*.patch
git diff --stat      # docs, games/ikaruga/ikaruga.toml, runtime/, translator/
```

y sigue con los pasos 3–5. Si ya tenías `build-rel`, basta con
`cmake --build build-rel --target ikaruga_boot --parallel "$(nproc)"`: [INFER] el cambio en el
traductor y en `ikaruga.toml` regenera `gen/` (no lo probé de forma incremental; si el hash de
abajo no sale, borra `build-rel/games/ikaruga/gen` y repite).

CONFIRMADO: el parche aplicado con `patch -p1` sobre `third_party/dream-recomp` (`51ca276`) deja
`runtime/`, `translator/`, `games/ikaruga/ikaruga.toml`, `docs/game-config.md` y
`docs/runtime-interrupts.md` idénticos (`diff -rq`) a los de `~/code/ajenos/dr-idle`, el árbol cuyo
`gen/` se midió.

Con el parche el binario de 5 ya no mide lo de la tabla: CONFIRMADO en la corrida de punta a punta
(docs/primer-uso.md) `ikaruga_boot` de 14 430 776 B, text 13 152 407, data 227 080, bss 590 520.

Comprobación igual que en 5.1. Esperado con el parche:
`924f8b67d9778173fba9dbcd15117eb36f84aa51ca355b6158837860aec884da` (CONFIRMADO sobre
`~/code/ajenos/dr-idle/build-rel/games/ikaruga/gen` con
`ls $G | sort | while read f; do cat "$G/$f"; done | sha256sum`). Sin el parche sigue siendo el de
5.1.

### 5.3 FIPR/FTRV/FMAC en `float` (`patches/dream-recomp-pc/0002-…`)

`git apply patches/dream-recomp-pc/*.patch` aplica también `0002`: FMAC, FIPR y FTRV en `float`
simple, el mismo código que la PSP (`patches/dream-recomp/0009-fpu-float.patch`; docs/velocidad-psp.md,
«Fila 9b»). Solo toca `runtime/include/dream/runtime/sh4/ops.h`, así que **el `gen/` no cambia**:
CONFIRMADO, con 0001+0002 aplicados sobre `51ca276` en `build/dream-recomp` el hash de 5.2 sale
`924f8b67d9778173fba9dbcd15117eb36f84aa51ca355b6158837860aec884da`. El binario sí cambia:
`ikaruga_boot` de 14 402 032 B (float).

Para comparar contra la evaluación de antes (FMA en FMAC, acumulación en `double`), un segundo
build del mismo árbol:

```sh
cmake -S . -B build-dbl -DDREAM_DEV_INTERPRETER=OFF -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS=-DDREAM_FPU_DOUBLE=1
cmake --build build-dbl --target ikaruga_boot --parallel "$(nproc)"   # 14 430 776 B
```

CONFIRMADO: el `build-dbl` da el mismo `write_hash` que `~/code/ajenos/dr-idle` (cuadros 0…3000,
sin entrada). Los dos lados compilan con `-ffp-contract=off -frounding-math`
(`cmake/DreamTargetDefaults.cmake`): sin FMA implícita.

`scripts/build-psp-ikaruga.sh` no necesita este paso para el runtime: aplica su propia copia de la
parte del runtime (`patches/dream-recomp/0004-idle-skip-runtime.patch`). Sí necesita el `gen/`
regenerado: el C++ con `idle_poll` no enlaza contra un runtime sin 0004, y el `gen/` sin `idle_poll`
compila pero nunca salta.

## 6. Correr

Opciones del lanzador usadas (SEGÚN `RESULTADO-IKARUGA.md`):

| opción | qué hace |
|---|---|
| `--config games/ikaruga/ikaruga.toml` | configuración del juego |
| `--vmu archivo.vmu` | tarjeta de memoria (se crea si no existe) |
| `--window` | ventana Vulkan 640×480 con vsync; sin ella corre headless sin límite de velocidad |
| `--press boton@frame,...` | botones simulados en frames dados |
| `--screenshot-at N` | guarda `screenshot-000.ppm` en el directorio actual en el frame N |
| `--max-seconds S` | se detiene a los S segundos de juego (`stop: time limit`, exit 0) |
| `--report archivo` | escribe el reporte final (contadores, `untranslated call targets`) |
| `--rtc-seed 1` | reloj fijo, para corridas repetibles |
| `--fps` | contador en pantalla (no va al log) |
| `--no-audio` | sin dispositivo de audio |

### 6.1 Headless (velocidad)

```sh
build-rel/games/ikaruga/ikaruga_boot --config games/ikaruga/ikaruga.toml \
  --vmu juego/ikaruga.vmu --rtc-seed 1 --no-audio --max-seconds 60 \
  --report juego/report-headless.txt
grep 'untranslated call targets' juego/report-headless.txt
```

Esperado: imprime `host: N s (Mx real time)` (formato CONFIRMADO en `runtime/boot/boot_main.cpp:742`); M ≈ 5. Referencias: 30 s de juego en 5,26 s =
5,7× (build dev, casi todo en el título; SEGÚN `RESULTADO-IKARUGA.md`) y 60 s en 11,7 s = 5,1×
incluyendo gameplay (SEGÚN `docs/framework-comparacion.md:19`). El reporte debe decir
`untranslated call targets: 0`.

### 6.2 Con ventana: título y capítulo 1

Las capturas salen en el directorio actual, por eso se corre desde `juego/`:

```sh
W=$PWD; cd juego
# Título: logo 斑鳩 / IKARUGA hacia el frame 600 (~10 s)
$W/build-rel/games/ikaruga/ikaruga_boot --config $W/games/ikaruga/ikaruga.toml \
  --vmu $W/juego/ikaruga.vmu --window --fps --rtc-seed 1 --max-seconds 15 \
  --screenshot-at 600 --report $W/juego/report-title.txt
mv screenshot-000.ppm titulo.ppm
# Capítulo 1: START/A simulados, captura en el frame 2600 (~43 s)
$W/build-rel/games/ikaruga/ikaruga_boot --config $W/games/ikaruga/ikaruga.toml \
  --vmu $W/juego/ikaruga.vmu --window --fps --rtc-seed 1 --max-seconds 50 \
  --press start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200 \
  --screenshot-at 2600 --report $W/juego/report-cap1.txt
mv screenshot-000.ppm capitulo1.ppm
cd $W
```

Qué se debe ver (SEGÚN `RESULTADO-IKARUGA.md`; esas dos capturas se hicieron con el build dev):

- `titulo.ppm`: fondo negro con el logo 斑鳩 / IKARUGA abajo a la derecha.
- `capitulo1.ppm`: gameplay del Capítulo 1: nave blanca entre estructuras, HUD con `1P`, medidor
  `CHAIN` y vidas.
- Con `--max-seconds 120` y `--screenshot-at 6500`, el build release llega a la pantalla
  `PLAY DATA / 1-PLAYER` (Chapter 1 = 1 500 puntos): la nave sin control pierde las vidas.
- ~60 fps en ventana (2 996 frames en 50 s); `untranslated call targets: 0`.

Controles a mano (SEGÚN `RESULTADO-IKARUGA.md`): flechas, Z/X/A/S, Enter.

## 7. Diferencias conocidas

- Sin `--bios` (volcado propio de la boot ROM) los textos de la tarjeta de memoria salen en
  blanco; el juego corre igual (SEGÚN `games/ikaruga/README.md:15` de dream-recomp).
- 6 accesos a memoria sin mapear en el frame 0, inocuos (SEGÚN `RESULTADO-IKARUGA.md`).
- No verificado aquí: jugar a mano, capítulos 2–5, audio a oído.
