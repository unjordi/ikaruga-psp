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
- Lee de `ms0:/PSP/GAME/IKARUGA/`: `1ST_READ.BIN` y `DISCO.ISO` (área de juego del GD-ROM,
  hecha con `scripts/preparar-disco-ikaruga.sh` desde el disco extraído; el disco nunca va al repo).
- Sin video, audio ni entrada (stubs). Cada 60 cuadros del guest agrega una línea a
  `ms0:/PSP/GAME/IKARUGA/ikaruga.log`: `frames`, `cycles` (SH-4, 200 MHz), `guest_s`,
  `host_s`, `speed = guest_s / host_s` (veces tiempo real) y `pc` del guest.

## Números

Variables: `G=/home/unjordi/code/ajenos/dr-rev/games/ikaruga/extracted`.

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
