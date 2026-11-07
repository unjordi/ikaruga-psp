# Fila 5a — arranque headless de Ikaruga en la PSP (medido en PPSSPP)

> **Advertencia:** todos los números son de **PPSSPP 1.20.4 (flatpak `org.ppsspp.PPSSPP`) sobre un
> Ryzen 9 9950X**, no de una PSP real. PPSSPP traduce el MIPS del Allegrex con su JIT y no modela
> la latencia de caché, de memoria ni del bus de la PSP: el número en hardware puede ser distinto y
> no se puede derivar de éste. Además `host_s` sale de `sceKernelGetSystemTimeWide`, que en PPSSPP
> es el reloj **emulado** de la PSP, no el reloj del PC.

## Qué corre

- EBOOT: `build/psp/ikaruga/EBOOT.PBP`, variante -Os, MEMSIZE=1
  (sha256 `26528a2b8379bf36ea1dd392e89a8f008cf1110ff7f749d280ab59a339242333`), construido con
  `scripts/build-psp-ikaruga.sh <dr-rev>/build-rel/games/ikaruga/gen` (dream-recomp `51ca276` +
  `patches/dream-recomp/`). Tamaño: imagen -Os 13,66 MB + guest 28,1 MB = 41,76 MB de ~52 MB.
- Lee de `ms0:/PSP/GAME/IKARUGA/`: `1ST_READ.BIN` y `DISCO.ISO` (área de juego del GD-ROM; el
  disco nunca va al repo). Esta medición usó el `DISCO.ISO` de `scripts/preparar-disco-ikaruga.sh`,
  que **no** es el disco real (los archivos caen en otros sectores y el juego diverge de PC desde el
  cuadro 4). Para jugar o comparar con PC usa el área HD real volcada con `tools/volcar-area-hd.cpp`
  (docs/correctitud-psp.md, «Disco»; receta en docs/primer-uso.md).
- Sin video, audio ni entrada (stubs). Cada 60 cuadros del guest agrega una línea a
  `ms0:/PSP/GAME/IKARUGA/ikaruga.log`: `frames`, `cycles` (SH-4, 200 MHz), `guest_s`,
  `host_s`, `speed = guest_s / host_s` (veces tiempo real) y `pc` del guest.

## Números

Revisión (2026-11-06 09:3x): EBOOT reconstruido desde cero y `scripts/test-psp-arranque.sh` repetido; mismos ciclos y PC en los cuadros 60–180 (CONFIRMADO).

Variables: `G=<dream-recomp>/games/ikaruga/extracted`.

### Velocidad y cuadros alcanzados (300 s de PPSSPP)

```sh
PROBAR_CAPTURA=build/psp/evidencia/juego-300s.png \
  scripts/probar-ikaruga-ppsspp.sh build/psp/ikaruga/EBOOT.PBP $G 300 build/psp/evidencia/juego-300s.log
```

| frames | cycles | guest_s | host_s | speed acumulada | pc |
|---:|---:|---:|---:|---:|---|
| 0 | 266 936 | 0,001 | 0,050 | — | 0x0c0100a6 |
| 60 | 201 989 964 | 1,010 | 16,314 | 0,0619x | 0x0c051028 |
| 180 | 602 355 154 | 3,012 | 44,582 | 0,0676x | 0x0c04b628 |
| 240 | 802 537 475 | 4,013 | 70,783 | 0,0567x | 0x0c03b320 |
| 420 | 1 403 084 964 | 7,015 | 174,546 | 0,0402x | 0x0c03cd60 |
| 600 | 2 003 632 481 | 10,018 | 277,454 | **0,0361x** | 0x0c0370ba |

- **En 300 s llegó al cuadro 600** (10,0 s de guest en 277,5 s de reloj PSP emulado).
- **Velocidad: 0,036x tiempo real acumulada** (≈ 28 s de PSP por segundo de Dreamcast);
  en el tramo 240→600 (después de la carga inicial) **0,029x** ((10,018−4,013)/(277,454−70,783)),
  es decir ≈ 34 s por segundo de juego. Para ir a tiempo real faltan ≈ 30–35x.
- La corrida de 60 s (`... $G 60 build/psp/evidencia/juego-60s.log`) da las mismas cuatro primeras
  líneas (cuadro 180 a los 44,6 s; ciclos iguales salvo ±1).

### Comparación con PC (mismo juego, mismo commit)

```sh
cd build/psp/evidencia/pc
B=<dr-rev>/build-rel/games/ikaruga/ikaruga_boot; C=<dr-rev>/games/ikaruga/ikaruga.toml
for f in 60 180 600; do rm -f v.vmu; $B --config $C --vmu v.vmu --max-frames $f --rtc-seed 1 \
  --no-audio --report rep-$f.txt; done
```

| frames | PC: cycles, pc | PSP (PPSSPP): cycles, pc |
|---:|---|---|
| 60 | 201 863 403, 0x0c051028 | 201 989 964, 0x0c051028 |
| 180 | 602 221 981, 0x0c04b628 | 602 355 154, 0x0c04b628 |
| 600 | 2 003 507 820, 0x0c0133d8 | 2 003 632 481, 0x0c0370ba |

El PC del guest coincide en los cuadros 60 y 180 y los ciclos difieren < 0,07 %: la PSP recorre el
mismo arranque. En el cuadro 600 los PCs difieren; el PC se toma en instantes distintos (en la PSP
en el callback de VBlank, en PC al parar tras el cuadro) y ahí el juego está en su bucle de cuadro,
así que no lo cuento como divergencia, pero no lo verifiqué. En PC el cuadro 600 es el título
(docs/medicion-pc.md), así que la PSP llega hasta el título; sin video no lo vi.

### Negativa: sin 1ST_READ.BIN

```sh
scripts/probar-ikaruga-ppsspp.sh build/psp/ikaruga/EBOOT.PBP - 15 build/psp/evidencia/sin-juego.log
```

```
ikaruga-psp: headless boot, log every 60 guest frames
error: cannot read ms0:/PSP/GAME/IKARUGA/1ST_READ.BIN; copy the game's 1ST_READ.BIN there
```

y el EBOOT sale con `sceKernelExitGame`.

### Prueba de aceptación

```sh
scripts/test-psp-arranque.sh build/psp/ikaruga/EBOOT.PBP $G 60
```

Corre las dos anteriores y falla si falta el error, si no hay cuadros, si el PC del guest no cambia
o si el guest se detuvo (`fault`/`stop`).

## Idle-skip en la PSP

Mismo EBOOT -Os para las dos columnas (sha256
`7f7c8dd35d8d1b5b0cd27f3e67a2f7603146aa6c5d2382757e5cc47c74f6e1e6`, imagen 13,66 MB + guest
28,1 MB = 41,76 MB de ~52 MB), construido con

```sh
scripts/build-psp-ikaruga.sh <dr-idle>/build-rel/games/ikaruga/gen
```

donde `gen/` sale del traductor con `patches/dream-recomp-pc/*.patch` (sha256 de la concatenación
ordenada `924f8b67d9778173fba9dbcd15117eb36f84aa51ca355b6158837860aec884da`; cómo generarlo en
docs/reproducir-pc.md §5.2) y el runtime lleva `patches/dream-recomp/0004-idle-skip-runtime.patch`.
`psp/ikaruga/main.cpp` llena `System::idle_loops` con la tabla `[idle]` de `ikaruga.toml`
(`0x0C0370BA = katana_wait_vblank`) igual que `boot_main.cpp`. Interruptor: si existe
`ms0:/PSP/GAME/IKARUGA/NO_IDLE_SKIP`, la tabla queda vacía (el equivalente de `--no-idle-skip`);
`scripts/probar-ikaruga-ppsspp.sh` lo crea con `PROBAR_SIN_IDLE=1`. El log dice
`idle-skip: on|off` al arrancar y cada línea `tick` añade `idle_skips` e `idle_cycles` (`pc` sigue
siendo el último campo).

```sh
G=<dream-recomp>/games/ikaruga/extracted
PROBAR_SIN_IDLE=1 scripts/probar-ikaruga-ppsspp.sh build/psp/ikaruga/EBOOT.PBP $G 300 build/psp/evidencia/sin-idle-300s.log
scripts/probar-ikaruga-ppsspp.sh build/psp/ikaruga/EBOOT.PBP $G 300 build/psp/evidencia/con-idle-300s.log
```

Mismas advertencias que arriba (PPSSPP 1.20.4 en el Ryzen 9 9950X, `host_s` = reloj PSP emulado).

| | sin idle-skip | con idle-skip |
|---|---:|---:|
| cuadro alcanzado en 300 s | 600 (a los 279,1 s) | **2520** (a los 298,2 s) |
| cuadro 600 a los | 279,1 s | 93,0 s |
| speed acumulada al final | 0,0359x | **0,141x** |
| speed en el tramo 240→600 | 0,0289x | 0,1525x (**5,3x** más) |
| speed en el tramo 600→2520 | — | 0,156x |
| ciclos saltados en 240→600 | 0 | 1 105 067 095 de 1 201 095 006 (92 %) |

Cuadros 60 y 180 (cycles, pc):

| frames | hoy (tabla de arriba) | sin idle-skip | con idle-skip |
|---:|---|---|---|
| 60 | 201 989 964, 0x0c051028 | 201 989 964, 0x0c051028 | 201 989 964, 0x0c051028 |
| 180 | 602 355 154, 0x0c04b628 | 602 355 153, 0x0c04b628 | 602 355 154, 0x0c04b628 |

- Sin idle-skip, el EBOOT nuevo da lo mismo que el de hoy: igual en el cuadro 60, ±1 ciclo en el
  180 y el cuadro 600 a los 279,1 s frente a 277,5 s.
- Con idle-skip no hay saltos hasta el cuadro 180 (`idle_skips 0`); empiezan entre los cuadros
  180 y 240.
- Ciclos del guest por cuadro, con idle-skip frente a sin él, en los 11 cuadros comunes (0–600):
  iguales en 0 y 60, +1 en 120–360 y en 480–600, −12 en 420. No son idénticos bit a bit, pero la
  diferencia está dentro de la que ya hay entre dos corridas **sin** idle-skip (cuadro 420: hoy
  1 403 084 964, corrida nueva sin idle-skip 1 403 084 975, +11). El `pc` muestreado en el callback
  de VBlank también varía entre corridas desde el cuadro 240 (cuadro 420: 0x0c03b320 sin
  idle-skip; 0x0c03cd60 con idle-skip y hoy).

## Runtime a `-O2`, generado a `-Os` (fila 9a)

Desde la fila 9a `build/psp/ikaruga/EBOOT.PBP` es la variante `mix` (runtime, stubs y `main.cpp` a
`-O2`; `gen/*.cpp` a `-Os`); la de las secciones anteriores (todo `-Os`) sale como
`build/psp/ikaruga/EBOOT-Os.PBP`. Las dos se construyen en la misma corrida, con el mismo `gen/` con
idle-skip de la sección anterior (sha256 de la concatenación `924f8b67…aec884da`):

```sh
scripts/build-psp-ikaruga.sh <dr-idle>/build-rel/games/ikaruga/gen --limpio
G=<dr-idle>/games/ikaruga/extracted
scripts/probar-ikaruga-ppsspp.sh build/psp/ikaruga/EBOOT-Os.PBP $G 120 build/psp/evidencia/fila9a-Os-120s.log
scripts/probar-ikaruga-ppsspp.sh build/psp/ikaruga/EBOOT.PBP $G 120 build/psp/evidencia/fila9a-mix-120s.log
```

EBOOTs: `EBOOT-Os.PBP` sha256 `488dde75fc53b05f6df19f641df75ea1047d18a9aee7235b7c6d1aa67799b3ec`
(imagen 13,66 MB + guest 28,1 MB = 41,76 MB), `EBOOT.PBP` (`mix`) sha256
`c11e79c10638ad43cadaf6a97c5e7353887217ef363c7ab4899a2b6f9a39e048` (13,75 MB + 28,1 MB = 41,85 MB de
~52 MB; `docs/tamano-psp.md` §5). Idle-skip activo en las dos. Mismas advertencias que arriba
(PPSSPP 1.20.4 en el Ryzen 9 9950X, `host_s` = reloj PSP emulado); una corrida de 120 s por variante.

| | todo `-Os` | `mix` | `mix` / `-Os` |
|---|---:|---:|---:|
| cuadro alcanzado en 120 s | 840 (a los 118,6 s) | **1140** (a los 117,5 s) | |
| cuadro 180 a los | 44,6 s | 31,6 s | 1,41x más rápido |
| cuadro 600 a los | 93,0 s | 70,6 s | 1,32x |
| cuadro 840 a los | 118,6 s | 91,4 s | 1,30x |
| speed acumulada al final | 0,118x | **0,162x** | |
| speed en el tramo 240→600 | 0,1525x | 0,1868x | 1,22x |
| speed en el tramo 600→840 | 0,1561x | 0,1921x | 1,23x |

Cuadros 60 y 180 (cycles, pc):

| frames | todo `-Os` | `mix` |
|---:|---|---|
| 60 | 201 989 964, 0x0c051028 | 201 989 964, 0x0c051028 |
| 180 | 602 355 154, 0x0c04b628 | 602 355 154, 0x0c04b628 |

- En los 15 cuadros comunes (0–840) `cycles`, `pc`, `idle_skips` e `idle_cycles` son idénticos en
  las dos corridas (`join` sobre las líneas `tick` de los dos logs).
- La corrida todo `-Os` reproduce la de la sección anterior (cuadro 600 a los 93,0 s en las dos).
- [INFER] La ganancia es mayor hasta el cuadro 180 (sin saltos de idle) que después, cuando el
  idle-skip ya quita la mayor parte de los ciclos y pesa más el código que no es `DcMemory`.

