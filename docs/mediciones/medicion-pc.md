> Foto de las mediciones del 2026-11-05; el estado vigente está en [`../plan.md`](../plan.md) y en el índice [`../README.md`](../README.md).

# Fila 4b — medir-pc: Ikaruga en el build release de dream-recomp (PC)

dream-recomp `51ca276`, sin cambios de código ni recompilación.

Host: AMD Ryzen 9 9950X (32 hilos; el runtime corre la emulación en un hilo), Linux x86_64.
Binario: `<dream-recomp>/build-rel/games/ikaruga/ikaruga_boot` (release,
`DREAM_DEV_INTERPRETER=OFF`, ELF no stripped, 14 425 888 B, BuildID `069c7ad7…`).
Config: `<dream-recomp>/games/ikaruga/ikaruga.toml`.
Todas las corridas: cwd = `medir/` del worktree (ignorado por git: `*.vmu`, logs y reportes no se commitean).

Variables usadas en todos los comandos:

```sh
cd <directorio-de-trabajo>
B=<dream-recomp>/build-rel/games/ikaruga/ikaruga_boot
C=<dream-recomp>/games/ikaruga/ikaruga.toml
P=start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200
```

## Revisión (2026-11-05 23:5x)

| Afirmación | Veredicto | Cómo |
|---|---|---|
| Gameplay del capítulo 1 a ≈4,6x tiempo real; 40 s y 80 s de juego en 7,54 s y 16,25 s de host | CONFIRMADO (±2 %) | repetido: 7,67 s y 16,44 s |
| `kmWaitVBlank` en guest `0x0c037080` | CONFIRMADO | `games/ikaruga/symbols.tsv:544` |
| ≈58 % del host del gameplay en la espera de VBlank; idle-skip daría 8–10x | SEGÚN la medición (muestreador propio, inclusivos con sesgo posible); el 8–10x es [INFER] | se confirma al implementar el idle-skip |
| Sin precisión doble en título + capítulo 1; FTRV/FIPR en funciones Ninja | SEGÚN la medición (conteo de entradas con gdb) | — |
| Cobertura ≈28 % de funciones / ≈20 % de bytes en 0–80 s | SEGÚN la medición | — |

La evidencia cruda (incluye el desensamblado del juego, dato que no va a ningún repo) está solo en la máquina de medición.

## 1. Velocidad: título vs. gameplay (veces tiempo real, headless, sin límite)

Headless ya corre sin límite de velocidad (lo dice el `--help`: el ritmo solo lo impone la ventana).
`--no-audio` apaga la *salida* de sonido; el AICA (ARM7 + DSP + mezclador) **sigue emulándose**: el
reporte de 80 s cuenta 232 522 361 instrucciones ARM y 3 528 116 muestras mezcladas. O sea, los números
incluyen el costo del audio.

Comando (una corrida por duración S; VMU nueva en cada una):

```sh
for s in 20 30 40 45 60 80 100 110; do rm -f v.vmu
  timeout 600 $B --config $C --vmu v.vmu --max-seconds $s --rtc-seed 1 --no-audio --press $P \
    --report rep-$s.txt > log-$s.log 2>&1
  grep -h '^guest\|^host\|^PVR' rep-$s.txt; done
```

40 s y 80 s se repitieron 3 veces (`rep-40-r{1,2,3}.txt`, `rep-80-r{1,2,3}.txt`): host 7,54 / 7,54 / 7,58 s y
16,25 / 16,24 / 16,36 s. Dispersión < 1 %, la corrida es determinista (mismos ciclos, frames y PC final).

| guest s | frames | host s (línea `host:` del reporte) | TA chunks acumulados | x tiempo real |
|---:|---:|---:|---:|---:|
| 20 | 1 198 | 3,49 | 147 255 | 5,7x |
| 30 | 1 797 | 5,37 | 1 527 398 | 5,6x |
| 40 | 2 397 | 7,54 | 12 330 398 | 5,3x |
| 45 | 2 697 | 8,69 | 15 397 543 | 5,2x |
| 60 | 3 596 | 11,70 | 23 960 597 | 5,1x |
| 80 | 4 795 | 16,25 | 41 603 675 | 4,9x |
| 100 | 5 994 | 20,29 | 50 675 202 | 4,9x |
| 110 | 6 593 | 22,27 | 53 461 916 | 4,9x |

Dónde está el gameplay: los "TA chunks" (geometría enviada al PVR) casi no suben hasta 20 s
(título/logo, 147 k) y se disparan entre 30 y 40 s (+10,8 M): ahí empieza el capítulo 1 (frame 2200 ≈ 36,7 s
es el último botón simulado). Desde ≈100 s el ritmo de TA cae (+2,8 M en 100–110 s): coincide con el final
de partida / pantalla PLAY DATA que vio la fila 0 a los 108 s. Por eso el **tramo de gameplay** que tomo es
**40–80 s** (y 40–100 s como cota).

Diferencias (velocidad del tramo = Δguest / Δhost):

| tramo | Δhost s | x tiempo real |
|---|---:|---:|
| 0–30 (título, logo, menús) | 5,37 | **5,58x** |
| 30–40 (transición a capítulo 1) | 2,17 | 4,60x |
| 40–60 (gameplay) | 4,16 | 4,80x |
| 60–80 (gameplay, más denso) | 4,55 | **4,39x** |
| **40–80 (gameplay)** | 8,71 | **4,59x** |
| 80–100 | 4,04 | 4,95x |
| 40–100 | 12,75 | 4,70x |

**Resultado 1:** en este x86 el gameplay del capítulo 1 corre a **≈4,6x tiempo real** (peor tramo de 20 s:
4,4x), contra ≈5,6x en el título. El gameplay cuesta ≈20 % más por segundo de juego que el título.
Cada segundo de juego cuesta ≈0,218 s de host en gameplay (8,71/40) y ≈0,179 s en el título (5,37/30).
Para la PSP lo que cuenta es el costo de gameplay, no el 5,7x del título que traía la fila 0.

Nota: la nave va sin control (solo los botones simulados), así que el "gameplay" incluye muertes y respawns;
la densidad de enemigos/balas no es la de una partida jugada en serio.

## 2. Perfil del gameplay

**`perf` no está instalado** (`which perf` → no encontrado; no hay `/usr/lib/linux-tools*`) y **`valgrind`
tampoco**. Hay `gdb` y `eu-stack`, pero muestrear con ellos detiene el proceso en cada muestra. Así que hice un
muestreador mínimo propio que **no toca dream-recomp**: una `.so` que se precarga con `LD_PRELOAD`, programa
`setitimer(ITIMER_PROF)` (tiempo de CPU del proceso) y en cada `SIGPROF` guarda el RIP interrumpido más un
`backtrace()`; al salir escribe las muestras y `/proc/self/maps`. Se simboliza con `nm -C` del binario (no
stripped). Fuentes: `medir/prof.c`, `medir/sym.py`, `medir/callers.py`, `medir/spin.py`, `medir/nest.py`,
`medir/nowait.py` (copiadas a la evidencia; no se commitean).

```sh
gcc -O2 -fPIC -shared -o libprof.so prof.c -lpthread
rm -f v.vmu
timeout 600 env PROF_OUT=$PWD/prof-gp.txt PROF_START=8 PROF_HZ=1000 LD_PRELOAD=$PWD/libprof.so \
  $B --config $C --vmu v.vmu --max-seconds 80 --rtc-seed 1 --no-audio --press $P --report rep-prof.txt
python3 sym.py prof-gp.txt.<pid> $B 25 > perfil-gp.txt
```

Ventana: empieza a muestrear a los 8 s de pared (= ≈40 s de juego según la tabla de §1) y termina a los 80 s
de juego → **cubre el tramo de gameplay 40–80 s**. 8 197 muestras a 1 kHz (≈8,2 s de CPU). El reporte con el
muestreador da `host: 16.21 s` contra 16,24–16,36 s sin él: el sobrecosto no se ve. El proceso tiene un solo
hilo que trabaja (todas las muestras son del mismo tid).

### 2.1 Por categoría (self = la hoja; inclusivo = en cualquier lugar de la pila)

| categoría | self | inclusivo |
|---|---:|---:|
| `DcMemory` (`read32`, `store<u32>`, `write32`, `sq_flush`, `read16`, `resolve`…) | **50,3 %** | 53,0 % |
| código generado (`dream::gen::*`) | **19,1 %** | 99,7 % |
| AICA mezclador (`Mixer::sample`, `dsp_step`, `Channel::stream_step`) | **10,8 %** | 10,8 % |
| llamadas indirectas (`call_indirect` + `find_function`) | **7,7 %** | 7,7 % |
| AICA ARM7 (`Arm7::step`/`run`) | **5,1 %** | 6,5 % |
| scheduler (`Scheduler::advance_to`, …) | 2,4 % | 20,4 % |
| AICA otro (`Aica::aram`, timers) | 1,4 % | 1,4 % |
| PVR/TA (`pvr::Ta::feed`, `Core::write_burst`, `ch2_dma`) | **0,9 %** | ≈1–2 % |
| excepciones/unwind | 0,4 % | 0,5 % |
| libc/libm/libstdc++ | 0,5 % | — |

Desglose de `DcMemory` (self): `read32` 22,9 %, `store<unsigned int>` 14,7 %, `write32` 5,9 %, `sq_flush` 4,5 %
(7,8 % incl.), `read16` 1,4 %, `resolve` 0,8 % self / 3,3 % incl. (`resolve` casi siempre queda *inlined* dentro
de `load`/`store`, así que su costo real está repartido en `read32`/`store`; no lo puedo separar sin perf con
info de línea). **AICA total ≈ 17,3 %** (mezclador 10,8 % — de ello DSP 6,8 % — + ARM7 5,1 % + otro 1,4 %), con
`--no-audio`. **`abi_bare`**: no aparece como símbolo en ninguna muestra; en este binario la llamada indirecta
es `dream::sh4::call_indirect` → `find_function` (7,7 % juntas).

**PVR por software:** en headless el runtime solo decodifica el TA (`Ta::feed`, 0,7 %) y no rasteriza nada: no
hay rasterizador por software en el perfil; la rasterización real va por Vulkan y solo con `--window`. Para la
PSP eso significa que **este perfil no dice cuánto cuesta dibujar**; solo cuánto cuesta *alimentar* el TA.

### 2.2 Top 25 por self (`perfil-gp.txt`)

| # | muestras | self | incl. | función |
|--:|--:|--:|--:|---|
| 1 | 1874 | 22,9 % | 22,9 % | `dream::mem::DcMemory::read32` |
| 2 | 1202 | 14,7 % | 14,8 % | `dream::mem::DcMemory::store<unsigned int>` |
| 3 | 557 | 6,8 % | 6,8 % | `dream::aica::Mixer::dsp_step` |
| 4 | 483 | 5,9 % | 5,9 % | `dream::mem::DcMemory::write32` |
| 5 | 436 | 5,3 % | 38,2 % | `dream::gen::kmyRequestCallback__resume` |
| 6 | 420 | 5,1 % | 5,1 % | `dream::sh4::find_function` |
| 7 | 370 | 4,5 % | 7,8 % | `dream::mem::DcMemory::sq_flush` |
| 8 | 367 | 4,5 % | 5,8 % | `dream::aica::Arm7::step` |
| 9 | 221 | 2,7 % | 57,9 % | `dream::gen::kmWaitVBlank__resume` |
| 10 | 208 | 2,5 % | 7,7 % | `dream::sh4::call_indirect` |
| 11 | 197 | 2,4 % | 10,8 % | `dream::aica::Mixer::sample` |
| 12 | 183 | 2,2 % | 6,3 % | `dream::gen::njCnkEasyVertexVN__resume` |
| 13 | 169 | 2,1 % | 20,1 % | `dream::sched::Scheduler::advance_to` |
| 14 | 132 | 1,6 % | 1,6 % | `dream::aica::Mixer::Channel::stream_step<…>` |
| 15 | 123 | 1,5 % | 7,7 % | `dream::gen::kmyiDefaultCallback__resume` |
| 16 | 111 | 1,4 % | 1,4 % | `dream::mem::DcMemory::read16` |
| 17 | 101 | 1,2 % | 10,8 % | `dream::gen::njCnkEasyTexture__resume` |
| 18 | 100 | 1,2 % | 1,2 % | `non-virtual thunk to dream::aica::Aica::aram` |
| 19 | 71 | 0,9 % | 19,7 % | `std::_Function_handler<…>` (callbacks del scheduler) |
| 20 | 66 | 0,8 % | 3,3 % | `dream::mem::DcMemory::resolve` |
| 21 | 59 | 0,7 % | 3,7 % | `dream::gen::njCnkEasyTextureClip__resume` |
| 22 | 57 | 0,7 % | 0,7 % | `dream::pvr::Ta::feed` |
| 23 | 55 | 0,7 % | 6,5 % | `dream::aica::Arm7::run` |
| 24 | 43 | 0,5 % | 8,2 % | `dream::gen::kmyiDefaultCallback` |
| 25 | 36 | 0,4 % | 19,1 % | `dream::gen::fn_0c019cd0__resume` |

### 2.3 El hallazgo: ≈58 % del gameplay es la espera de VBlank del Kamui

`kmWaitVBlank` (Kamui2 SDK, guest `0x0c037080`, según `symbols.tsv`) está en la pila del **57,9 %** de las
muestras (`python3 spin.py …`, `nest.py …`). El código generado de ese bucle (`gen/ikaruga.cpp`, bloques
`L_0c0370b2`…`L_0c0370d2`) hace en cada vuelta 2 `call_indirect` a `r14` (que resultan ser
`kmyRequestCallback`, `0x0c03cd60`, que a su vez hace otro `call_indirect` por una tabla en `0x8c10d64c`) y
5–6 `m.read32` de variables en la pila / RAM, hasta que una interrupción cambia el contador. Es decir: **el juego
termina su frame y gira esperando el VBlank, y en el recompilador ese giro cuesta tiempo real de host.**

- 44,6 % de todas las muestras: dentro de `kmWaitVBlank` sondeando (sin scheduler/IRQ encima).
- 13,3 %: dentro de `kmWaitVBlank` pero atendiendo scheduler/IRQ/dispositivos (trabajo real que caería igual).
- 42,1 %: fuera de la espera = el trabajo real del frame.
- El 97–99 % de `find_function` y `call_indirect` y ≈65 % de `read32` vienen de esa espera (`callers-gp.txt`).

Perfil **fuera de la espera** (3 453 muestras, `nowait-gp.txt`): `DcMemory` 54,7 % (`read32` 18,1 %, `store<u32>`
14,1 %, `sq_flush` 10,7 %, `write32` 6,1 %, `read16` 3,2 %, `resolve` 1,9 %), código generado 20,8 %
(`njCnkEasyVertexVN` 5,3 %, `njCnkEasyTexture` 2,9 %, `njCnkEasyTextureClip` 1,7 % — Ninja chunk-model
rendering), AICA mezclador 8,9 % + ARM7 3,8 % + otro 1,2 % = 13,9 %, scheduler 4,1 %, PVR/TA 2,1 %, llamadas
indirectas 0,4 %.

**Consecuencia para la PSP [INFER]:** el 4,6x del §1 subestima el margen: más de la mitad del host se va en
el giro de espera. Un atajo de "idle skip" de `kmWaitVBlank` (saltar `cycles` al próximo evento cuando el bucle
no progresa) dejaría el trabajo real del frame en ≈42 % + parte del 13 % → del orden de **8–10x tiempo real** en
este x86. Lo siguiente en la lista es el acceso a memoria del trabajo real (`read32`/`store`/`sq_flush` ≈ 50 %
del trabajo útil: fast path de RAM y de store queues) y luego AICA (≈14 % del trabajo útil con `--no-audio`).
Las llamadas indirectas importan casi solo dentro de la espera.

## 3. FPU: ¿precisión doble (FPSCR.PR=1)? ¿FIPR/FTRV?

### 3.1 Estática: notas del traductor y código generado

```sh
R=<dream-recomp>/build-rel/games/ikaruga/gen/ikaruga.report.txt
head -11 $R
grep -c "FPSCR.SZ unknown" $R; grep -o "note: [^:]*: [^:]*: FPSCR\.[A-Z]* [a-z]*" $R | awk '{print $NF,$(NF-1)}' | sort | uniq -c
G=<dream-recomp>/build-rel/games/ikaruga/gen/ikaruga.cpp
grep -o "fp mode pr=[01?] sz=[01?]" $G | sort | uniq -c
for op in fipr ftrv fschg frchg "lds r[0-9]*,fpscr" fsca fsrra fcnvsd fcnvds; do
  echo "$op: $(grep -cE "^ *// 0x[0-9a-f]+: $op" $G)"; done
```

- Reporte del traductor: 5 264 funciones, 413 007 instrucciones, **49 862 "fp runtime branches"**, 12 "unknown fpscr
  summaries", `entry modes inferred 0 (ambiguous 5257)`. Notas: 37 493 × "FPSCR.SZ unknown" (todas en `fmov`) y
  12 369 × "FPSCR.PR unknown" (`fmul` 3 287, `fadd` 2 908, `fcmp` 2 464, `fsub` 2 171, `float` 936, `ftrc` 382,
  `fdiv` 216, `fsqrt` 5…). En el C++ generado: 43 146 bloques con `fp mode pr=? sz=?` y solo 90 con `pr=0 sz=0`.
  O sea, el traductor **no sabe** el modo y emite cada op FP con las dos ramas, p. ej.
  `if (c.fpscr & FPSCR_PR) { c.fpul = ftrc(get_dr(c, 2)); } else { c.fpul = ftrc(c.fr[3]); }`.
  El "unknown" es del análisis, **no** evidencia de que el juego use doble.
- Sitios en el código guest (comentarios `// 0x…:` del .cpp): `fipr` 62, `ftrv` 92, `fschg` 576, `frchg` 33,
  `lds Rn,fpscr` 54, `fsca` 15, `fsrra` 20, `fcnvsd` 1, `fcnvds` 0. Nada en los 4 `reloc_*`.
- ¿Quién pone PR=1? Leí la constante de pool que precede a cada `lds Rn,fpscr` en `1ST_READ.BIN`
  (`medir/fpscr.py` + listado completo en la evidencia). Solo **5 sitios cargan un literal `0x00080000` (PR=1)**:
  `log10_0c062f70` (0x0c062f84, 0x0c062fae), `fn_0c063138` (0x0c063220) y `xtostr` (0x0c063d9e, 0x0c063dca) —
  la libm/printf de doble precisión de la librería C de Hitachi (`fpscr.py` contó 6 porque una ventana de
  búsqueda de 12 instrucciones agarró el mismo literal dos veces). Otros `lds` sin literal cercano (p. ej. en
  `fn_0c062fba`, vecina de `log10`) restauran un FPSCR guardado; no los seguí uno por uno. El resto de los `lds` son máscaras que **limpian** PR/SZ
  (`0xffe7ffff`, `0xfff7ffff`, `0xffcfffff`), cambian FR (`0x00200000`, banco para `ftrv`), o ponen DN
  (`0x00040000`, `buFbrExecFiberServer`). FIPR/FTRV están en 44 funciones, todas de la librería **Ninja**
  (`njCalcPoint*`, `njCnk*Vertex*`, `njRotate*`, `njTranslate*`, `njMultiMatrix`, `njQuaternionEx`…).

### 3.2 En ejecución (título + capítulo 1, 0–80 s)

Con el mismo script de gdb de la §4 en modo conteo (breakpoint que cuenta sin parar en cada entrada de la
función; `medir/cov.py`, `COV_ONLY_COUNT=1`):

```sh
COV_BIN=$B COV_OUT=$PWD/fpcount-80.txt COV_COUNT=$PWD/count-names.txt COV_ONLY_COUNT=1 \
  timeout 2400 gdb -batch -nx -x cov.py --args $B --config $C --vmu v.vmu --max-seconds 80 \
  --rtc-seed 1 --no-audio --press $P --report rep-fpcount.txt      # 114 s de pared
```

Llamadas en 80 s de juego (wrapper; la cifra `__resume` es igual o casi):

| función (usa FIPR/FTRV) | llamadas | ≈ por frame (4 795) |
|---|---:|---:|
| `njTranslateEx` (1 ftrv) | 400 533 | 84 |
| `njCnkModelClip` (1 ftrv) | 331 363 | 69 |
| `njRotateEx` (6 ftrv, 3 fsca) | 257 893 | 54 |
| `njCnkEasyVertexVN` (1 ftrv + 4 fipr, con bucle por vértice) | 160 711 | 34 |
| `njTranslate` | 101 102 | 21 |
| `njRotateY` / `Z` / `X` | 95 209 / 89 559 / 77 859 | ≈55 juntas |
| `njCnkEasyMultiVertexVN` | 64 821 | 14 |
| `njCalcPointT3DP` | 62 999 | 13 |
| `njCnkSimpleVertexVN` | 60 066 | 13 |
| `njCalcPointEx`, `njCalcVector`, `njCnkEasyVertexVD8`, `njCalcPoint`, `njPostMultiMatrix` | 18 296, 9 027, 5 765, 5 293, 830 | |

16 de las 44 funciones con FIPR/FTRV se ejecutaron. Las de PR=1: `log10_0c062f70`, `fn_0c062fba`,
`fn_0c063138`, `xtostr`, `fn_0c04c390` → **0 entradas** en 0–80 s (cobertura §4, `grep -c` sobre
`cov-30.txt`/`cov-80.txt`). `fmtout` (printf) sí corre 8 100 veces pero su `lds` **limpia** PR
(`and 0xfff7ffff`); `longjmp` (9 442) restaura un FPSCR guardado y `buFbrExecFiberServer` pone DN.

**Resultado 3:** en el título y el capítulo 1 **Ikaruga no entra a precisión doble** (ninguna función que pone
PR=1 se ejecuta; solo existen en la libm/printf de doble). **Sí usa FTRV/FIPR, mucho**: ≈1,6 M llamadas a
funciones Ninja con FTRV en 80 s (~350 por frame contando sólo entradas; las instrucciones son más porque
`njCnk*Vertex*` las ejecuta por vértice). Para la PSP: las matrices/vértices Ninja son el candidato natural a la
VFPU (FTRV = `vtfm4.q`, FIPR = `vdot.q`), y las ≈50 000 ramas "PR?/SZ?" del traductor se podrían quitar fijando
PR=0 en todo lo que no sea esa libm.

## 4. Cobertura: cuánto del código traducido se ejecuta (título + capítulo 1)

El runtime **no tiene contador ni traza de funciones** en el build release (`grep -rniE
"coverage|trace[-_]calls|call_count|hit_count|--trace" runtime/boot/boot_main.cpp runtime/src/sh4/*.cpp` solo
encuentra un comentario sobre builds con `--trace` del traductor, que exige regenerar y recompilar). `perf` no
está. Usé **gdb**: un breakpoint interno por cada símbolo `dream::gen::*` (wrapper y `__resume`, 10 540
símbolos para 5 270 funciones guest) que anota el primer golpe y se borra solo (`medir/cov.py`), y luego
`covsum.py` cruza contra los `*.functions.json` del traductor (5 264 + 6 funciones de los `reloc_*`,
1 185 312 bytes guest).

```sh
COV_BIN=$B COV_OUT=$PWD/cov-30.txt timeout 1700 gdb -batch -nx -x cov.py --args $B --config $C --vmu v.vmu \
  --max-seconds 30 --rtc-seed 1 --no-audio --press $P --report rep-cov30.txt
COV_BIN=$B COV_OUT=$PWD/cov-80.txt timeout 1700 gdb -batch -nx -x cov.py --args $B --config $C --vmu v.vmu \
  --max-seconds 80 --rtc-seed 1 --no-audio --press $P --report rep-cov.txt     # 26 s de pared
python3 covsum.py cov-30.txt cov-80.txt > cobertura.txt
```

Las corridas bajo gdb terminan con los mismos ciclos/frames/PC que sin gdb (`16000009449 cycles, 4795
frames, pc 0x0c0196fc` en 80 s; `6000011465 cycles, 1797 frames` en 30 s): es la misma ejecución.

| tramo | funciones ejecutadas | % funciones | bytes guest | % bytes |
|---|---:|---:|---:|---:|
| 0–30 s (arranque, logo, título, menús) | 1 074 de 5 270 | 20,4 % | 164 258 | 13,9 % |
| 0–80 s (+ capítulo 1 hasta 80 s) | 1 452 de 5 270 | **27,6 %** | 238 072 | **20,1 %** |
| nuevas solo por el gameplay (30–80 s) | 378 | | 73 814 | |

Frías en 0–80 s: **3 818 funciones (72 %), ≈947 KB de 1,19 MB (80 %)**; 708 de ellas tienen nombre del SDK.
Las nuevas del gameplay son las esperables: `njCnkEasy*` (dibujo de modelos chunk), `njCalc*`,
`in_atan2s`/`frexp`/`in_fmods`, `kmSetFogDensity`, liberación de texturas VQ. Las frías más grandes son
`fn_*` sin nombre de 4–6 KB en `0x0c08f…`–`0x0c0b1…` (p. ej. `fn_0c09d750` 6 080 B, `fn_0c08fe04` 5 162 B;
`fn_0c0ca318`/`fn_0c0ca31c` casi solapadas — huele a entradas falsas o a código de otros capítulos/modos).

**Resultado 4:** con el título y ≈40 s del capítulo 1 corre **≈28 % de las funciones / ≈20 % de los bytes**.
Para la PSP [INFER]: el 80 % de los bytes guest no se tocó en esta corrida. Pero los capítulos 2–5, jefes,
opciones, guardado y la pantalla de fin **no se cubrieron**, así que "frío aquí" ≠ "muerto": la cifra sirve
para priorizar qué optimizar (y qué podría vivir en un overlay), no para borrar código.

## Resumen para decidir qué optimizar en la PSP (con números de Ikaruga)

1. **Idle-skip de `kmWaitVBlank`**: ≈58 % del tiempo de host del gameplay está en esa espera (44,6 % sondeo puro).
2. **Acceso a memoria (`DcMemory`)**: 50 % del total y 55 % del trabajo fuera de la espera; `read32` y
   `store<u32>` primero, luego `sq_flush` (10,7 % del trabajo útil: el juego alimenta el TA por store queues).
3. **AICA**: ≈17 % del total / ≈14 % del trabajo útil aun con `--no-audio` (DSP del mezclador 6,8 %, ARM7 5 %).
4. **FPU**: sin doble precisión en ejecución; FTRV/FIPR en ≈1,6 M llamadas Ninja en 80 s → VFPU; quitar las
   ≈50 000 ramas PR?/SZ?.
5. **Llamadas indirectas**: 7,7 % del total, pero casi todo dentro de la espera; fuera de ella, 0,4 %.
6. PVR: alimentar el TA cuesta ≈1–2 %; el costo de *dibujar* no está medido (headless no rasteriza).

## Evidencia

Los scripts de medición (`prof.c`, `sym.py`, `cov.py`, `covsum.py`, `fpscr.py`), los reportes y los perfiles crudos quedaron fuera del repo: incluyen datos derivados del juego.

## Qué NO verifiqué y dónde está mi punto ciego

- **No usé `perf` ni `callgrind`** (no están instalados). El perfil sale de un muestreador propio con
  `SIGPROF` + `backtrace()` de glibc: (a) `backtrace()` desenrolla con la info de unwind; en las funciones
  generadas enormes con `goto` puede cortar o saltarse marcos, así que los **inclusivos** (sobre todo la
  atribución a `kmWaitVBlank`) pueden estar sesgados; los **self** (RIP interrumpido) son fiables;
  (b) `resolve` queda inlined y no lo separé de `read32`/`store`: el % de `resolve` que pedía la tarea no está
  medido como tal; (c) 8 197 muestras, error de ±≈0,5 pp en las categorías grandes.
- **El 58 % de espera de VBlank sale del perfil más la lectura del código generado**; no lo comprobé
  quitando la espera (habría que cambiar código, y estaba prohibido). El "8–10x" con idle-skip es [INFER].
- **Ventana de gameplay**: elegí 40–80 s por el salto de los TA chunks y por la fila 0; no miré capturas en esta
  fila. La nave va sin control: muere y reaparece, así que la carga no es la de un jugador real ni la de los
  capítulos posteriores.
- **PVR por software / costo de dibujo: no medido.** Headless no rasteriza; con ventana el dibujo va por Vulkan.
  Para la PSP (GE), el costo de traducir listas TA → GE es un hueco total de esta medición.
- **Audio**: `--no-audio` mantiene ARM7/DSP/mezclador; no medí con audio encendido ni sin AICA.
- **FPU en ejecución**: conté **entradas** a funciones (gdb), no instrucciones FTRV/FIPR ejecutadas ni el valor
  real de FPSCR.PR en cada op. Que PR=1 no ocurra se apoya en esto: solo hay 5 literales PR=1 en el binario y sus
  funciones tienen 0 entradas. Un `lds` que restaura un FPSCR guardado (`longjmp`, ISR) podría traer PR=1; no lo
  instrumenté.
- **Cobertura**: breakpoint en el símbolo C++. Si el compilador **inlineó** una función generada (son `static`),
  sus ejecuciones inlined no pegan → la cobertura puede estar **subestimada**. Medí por función, no por bloque,
  y no cubrí los capítulos 2–5.
- Host: Ryzen 9 9950X; los "x tiempo real" se trasladan a la PSP (MIPS a 333 MHz) solo como proporciones.
