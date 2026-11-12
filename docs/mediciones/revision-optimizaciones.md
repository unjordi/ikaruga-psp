> Foto de las mediciones del 2026-11-06; el estado vigente está en [`../plan.md`](../plan.md) y en el índice [`../README.md`](../README.md).

# Revisión: diseño vs. medido, y optimizaciones posibles para ikaruga-psp

> **Revisión (2026-11-06 10:3x):** CONFIRMADO por segunda mano: `fesetround` de newlib-psp es un stub (`jr ra; li v0,-134`), así que el modo de redondeo del guest no se aplica; FIPR/FTRV acumulan en `double` (`runtime/include/dream/runtime/sh4/ops.h:338-355`) y el ELF tiene 6 328 llamadas a `__muldf3`/`__adddf3`. Sin repetir: que `read32` a `-Os` llame a `memcpy` (SEGÚN este documento). Lo citado de `animal_forest_psp` ya no se puede verificar (no se conserva ese clon).


Fecha: 2026-11-06. Base: repo ikaruga-psp `develop` `af95045`; dream-recomp `51ca276` (árbol de medición `dr-rev`, y
el árbol con idle-skip `dr-idle`); ELF de la fila 5a (`ikaruga-Os.elf` / `ikaruga.elf` del worktree `f5a`), toolchain
`ikaruga-psp-toolchain:v20261001` (psp-gcc 15.2).

Etiquetas: **CONFIRMADO** (abierto o corrido: dónde), **SEGÚN <fuente>**, **[INFER]**.
Rutas: `dr/…` = dream-recomp `51ca276`; `repo/…` = ikaruga-psp `develop`; `elf-Os` / `elf-O2` = esos ELF desensamblados
con `psp-objdump -d -C` dentro de la imagen del toolchain. Las mediciones nuevas de esta revisión quedaron en
`/tmp/rev-opt/` de la cachy (reportes y perfiles; contienen datos derivados del juego y no van a ningún repo).
Ports de referencia: `sf64-psp`, `oot-psp` (citados en el diseño, hoy `docs/plan.md`) y `shar-psp-engine` (GPL-3.0,
`LICENSE` abierto: solo se describe el enfoque, no se copia código). `animal_forest_psp` lo abrí brevemente antes de que
se borrara de la cachy; lo que cito de él es SEGÚN esa lectura y ya no se puede re-verificar.

## 0. Lo más importante

1. **En la PSP, el costo que probablemente domina el gameplay no aparece en el perfil de PC: la aritmética `double` es
   por software.** FIPR y FTRV acumulan en `double` (`dr/runtime/include/dream/runtime/sh4/ops.h:340-363`). En el
   Allegrex cada FIPR son 16 llamadas a libgcc (8 `__extendsfdf2`, 4 `__muldf3`, 3 `__adddf3`, 1 `__truncdfsf2`) y cada
   FTRV 64 (CONFIRMADO: `elf-Os`, `dream::sh4::fipr` 0x0932eec4 y `ftrv` 0x0932f034, fuera de línea). `fmac` → `fmaf` de
   newlib, que también pasa por `double` (6 llamadas, `elf-Os` 0x0940e058). En x86 eso es hardware.
2. **Bug: el modo de redondeo del guest nunca se aplica en la PSP.** `fesetround` de newlib-psp es `jr ra; li v0,-134`
   (CONFIRMADO, `elf-Os` 0x0940e178): no hace nada y devuelve error. El guest arranca con FPSCR `0x00040001` (redondeo a
   cero, `dr/runtime/src/system.cpp:32`, `dr/runtime/src/hle/bios.cpp:153`), así que en la PSP las operaciones
   `float` redondean distinto que en el Dreamcast y en el PC. `repo/patches/dream-recomp/README.md` afirma lo contrario.
3. **El runtime se compila a `-Os` junto con el generado, y a `-Os` cada `read32` de RAM hace `jal resolve` + `jal
   memcpy` de la libc para copiar 4 bytes** (CONFIRMADO: `elf-Os` `DcMemory::read32` 0x0880a4a0, instrucciones en
   0x880a54c y 0x880a56c). El runtime a `-O2` cuesta +85 KB (SEGÚN `repo/docs/mediciones/tamano-psp.md` §2: 486 785 B contra
   401 579 B). Es el arreglo más barato de toda la lista.
4. **Con el idle-skip, el gameplay en PC queda en 42 % `DcMemory` + 29 % AICA + 17 % código generado** (CONFIRMADO:
   perfil nuevo de esta revisión, §1.4). El AICA a 44,1 kHz (un evento del scheduler por muestra, DSP de 128 pasos por
   muestra) es del orden de todo el presupuesto de un núcleo de 333 MHz [INFER, §2.4].
5. **El TA se alimenta con ≈920 000 ráfagas de store queue por segundo de gameplay** (CONFIRMADO, contadores del
   reporte, §1.4), y cada ráfaga pasa por una búsqueda lineal en la UTLB (64 entradas), `resolve`, una búsqueda lineal
   de MMIO, `on_device_access` (que recorre los eventos del scheduler) y un `vector::insert`. En la PSP eso es
   cientos de millones de instrucciones por segundo [INFER, §2.2].

## 1. Diseño contra realidad

### 1.1 Tabla

`diseno-psp.md` = el diseño de la fila 1, absorbido en `docs/plan.md` (las filas de su tabla 1 son las piezas 1–13 citadas en los pasos). «Medido» cita el documento o la corrida de donde sale.

| # | Supuesto del diseño | Cifra del diseño | Medido | Veredicto |
|---|---|---|---|---|
| 1 | El guest ocupa 28,1 MB residentes desde el inicio | 28,1 MB (§2) | 28,1 MB en la suma del build (SEGÚN `tamano-psp.md` §3); los seis `new uint8_t[N]()` siguen ahí (CONFIRMADO `dr/runtime/src/mem/dc_memory.cpp:114-119`) | CONFIRMADO |
| 2 | Código traducido en MIPS | 13–20 MB (§2) | `-O2`: 22,39 MB del generado (24,65 MB el ELF); `-Os`: 11,47 MB (13,65 MB el ELF) (SEGÚN `tamano-psp.md` §2) | CORREGIDO: `-O2` queda arriba del rango, `-Os` abajo |
| 3 | Suma total contra ≈52 MB | 44–53 MB (§2) | `-Os` ≈44–46 MB; `-O2` ≈55–57 MB (SEGÚN `tamano-psp.md` §3) | CONFIRMADO para `-Os`; `-O2` no cabe |
| 4 | Velocidad en la PSP sin optimizar | 0,1–0,25× tiempo real (§3) | 0,036× acumulado y 0,029× en el tramo de cuadros 240→600 (título), en PPSSPP (SEGÚN `arranque-psp.md`) | DESMENTIDO: 3–8× peor que lo previsto, y PPSSPP no modela caché ni latencia de memoria, así que en hardware es probable que salga más lento [INFER] |
| 5 | Relación entre un núcleo x86 y el Allegrex | 20–50× (§3) | PC título 5,58× (SEGÚN `medicion-pc.md` §1) contra PPSSPP 0,029–0,036× ⇒ ≈155–190× | DESMENTIDO. Parte de la diferencia es propia del port (llamadas virtuales y `memcpy` por acceso, contadores de 64 bits, `double` por software, §2) y no del hardware [INFER] |
| 6 | `DcMemory` es el primer costo | 56 % (Crazy Taxi) | Ikaruga sin idle-skip 50,3 % self; con idle-skip 42,0 % self / 46,3 % incl. (§1.4) | CONFIRMADO |
| 7 | AICA | ≈10 % (Crazy Taxi) | 17,3 % sin idle-skip; **29 %** con idle-skip (mezclador 18,9 %, de ello DSP 11,4 %; ARM7 10,0 % incl.) (§1.4) | CORREGIDO al alza: con el idle-skip es el segundo costo |
| 8 | Llamadas indirectas: 5,9 M/s, ≈35 % de los 333 MHz | (§3) | 7,7 % del total, 97–99 % dentro de la espera de VBlank (SEGÚN `medicion-pc.md` §2.3); con idle-skip 0,8 % (§1.4) | DESMENTIDO como prioridad: la caché directa ya está y el idle-skip quita casi todas |
| 9 | Doble precisión del SH-4 | «no revisé» (§3) | Ikaruga no entra a PR=1 en 0–80 s (SEGÚN `medicion-pc.md` §3) | CONFIRMADO, pero el runtime usa `double` por su cuenta en FIPR/FTRV/FMAC (§0.1): en la PSP eso es soft-float |
| 10 | FIPR/FTRV → VFPU | (§3) | ≈1,6 M llamadas a funciones Ninja con FTRV en 80 s (SEGÚN `medicion-pc.md` §3.2) | CONFIRMADO y sube de prioridad por el punto 9 |
| 11 | `-O2` en el código generado es obligatorio | (§3) | No cabe (fila 3 de esta tabla) | CORREGIDO: `-Os` en lo frío, `-O2` en lo caliente (§2.6) |
| 12 | Evitar excepciones C++ en la ruta caliente | (§3) | 16 354 `__cxa_throw` en 80 s = 204/s (CONFIRMADO: gdb con breakpoint de conteo, `/tmp/rev-opt/throw.log`); `task-switching RTEs: 0` (reporte) | CONFIRMADO que existen y son frecuentes; costo en la PSP sin medir |
| 13 | ARM7 ≈3 MIPS | 2,9 MIPS (§5) | 232 522 361 instrucciones ARM en 80 s = 2,9 MIPS (CONFIRMADO, `/tmp/rev-opt/rep-throw.txt`) | CONFIRMADO |
| 14 | El AICA se puede mover al ME porque el acoplamiento es por memoria | (§5, opción A) | El SH-4 escribe 2 registros AICA en 80 s, el ARM 1 966 493; 0 interrupciones en cada sentido (`SCPU sh4->arm 0, arm->sh4 0`, `0 SH-4 irq raises`) (CONFIRMADO, mismo reporte) | CONFIRMADO: todo el diálogo SH-4↔ARM pasa por ARAM. Pero el ME no tiene VFPU ni E/S, su caché no es coherente y PPSSPP no ejecuta código del ME (SEGÚN `shar-psp-engine/docs/research/media-engine.md:18-21`): no se puede validar en el emulador |
| 15 | Variantes `ikaruga_reloc_*` como overlays | (§2 recorte 2) | 3,5 KB, 2,5 KB, 2,5 KB y 5,7 KB de C++ (CONFIRMADO `ls -la` de `gen/`) | DESMENTIDO: no vale la pena |
| 16 | Código no ejecutado como candidato a overlay | (§2 recorte 2) | 0–80 s ejecuta 20,1 % de los bytes guest (SEGÚN `medicion-pc.md` §4) | CONFIRMADO como dato; ver §2.6 (prefiero `-Os` en lo frío a overlays) |
| 17 | Recorte de la Boot ROM (2 MB) | (§2 recorte 1) | Sigue reservada aunque no se cargue BIOS (CONFIRMADO `dc_memory.cpp:117`; `repo/psp/ikaruga/main.cpp` no carga BIOS). Sin BIOS los mensajes de la VMU del título salen en blanco (SEGÚN `framework-comparacion.md`, revisión) | Vigente; ver §2.5 |
| 18 | Un solo hilo en el runtime | (§0) | — | CONFIRMADO (sin cambios) |
| 19 | El perfil de PC sirve para priorizar la PSP | implícito | El PC no paga soft-float, ni `memcpy` fuera de línea, ni contadores de 64 bits, ni llamadas virtuales tan caras | CORREGIDO: hace falta un perfil o contadores en el propio Allegrex (§2.9) |

### 1.2 Decisiones que conviene cambiar

1. **Banderas por grupo en lugar de una sola `OPT`.** `repo/psp/ikaruga/Makefile:8` y `:38` aplican `$(OPT)` al
   runtime, a los stubs y al generado. Runtime y stubs a `-O2` siempre; el generado `-Os` con lo caliente a `-O2`.
2. **FIPR/FTRV/FMAC sin `double` en la PSP.** Ninguna de las dos alternativas del diseño (VFPU) es exacta respecto
   del PC; hay que decidir cuánta fidelidad bit a bit se exige (§2.1, A1).
3. **El AICA no puede validarse «primero en el núcleo principal»** (diseño §5 y §6 paso 7) a fidelidad completa:
   hace falta un modo reducido (DSP apagado, eventos en lotes) desde el primer día que haya audio, y el ME solo se
   mide en hardware.
4. **Quitar las llamadas indirectas de la lista de prioridades** y añadir en su lugar el camino del TA por store
   queues (§2.2).
5. **PPSSPP no es oráculo de velocidad** para lo que más importa aquí (latencia de memoria, caché de 16 KB, ME). Las
   decisiones de rendimiento necesitan contadores en el EBOOT y, al final, hardware.
6. **Overlays: no por ahora.** Con `-Os` el ELF cabe con 6–8 MB de margen; la mezcla `-Os`/`-O2` cuesta ≈2–3 MB
   (§2.6). Overlays solo si texturas + audio + video se comen ese margen.

### 1.3 Supuestos del diseño que siguen sin medir

TATE (§4 del diseño), costo de dibujar en el GE, costo real de las excepciones en el Allegrex, y qué fracción del
tiempo de la PSP se va en soft-float. Las cuatro tienen medición propuesta en §2.

### 1.4 Medición nueva: perfil del gameplay con idle-skip (PC)

Binario `dr-idle/build-rel/games/ikaruga/ikaruga_boot` (dream-recomp `51ca276` + el parche de idle-skip,
`repo/patches/dream-recomp-pc/0001-…`), muestreador `libprof.so` de la fila 4b (`fila4b-evidencia/medir/prof.c`,
`sym.py`). Ventana: desde el host-segundo en que el guest llega a 40 s hasta 80 s de guest.

```sh
cd /tmp/rev-opt; M=<fila4b-evidencia>/medir; B=<dr-idle>/build-rel/games/ikaruga/ikaruga_boot
C=<dr-idle>/games/ikaruga/ikaruga.toml; P=start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200
$B --config $C --vmu v.vmu --max-seconds 40 --rtc-seed 1 --no-audio --press $P --report rep-i40.txt   # host 3,48 s
env PROF_OUT=$PWD/prof-i.txt PROF_START=3.48 PROF_HZ=2000 LD_PRELOAD=$M/libprof.so \
  $B --config $C --vmu v.vmu --max-seconds 80 --rtc-seed 1 --no-audio --press $P --report rep-i80.txt  # host 9,74 s
python3 $M/sym.py prof-i.txt.<pid> $B 30 > perfil-i.txt
```

Resultado: gameplay 40–80 s en 6,26 s de host = **6,4×** (sin idle-skip 4,59×, SEGÚN `medicion-pc.md` §1). 6 096
muestras.

| categoría | self | inclusivo |
|---|---:|---:|
| `DcMemory` | **42,0 %** | 46,3 % |
| AICA mezclador (DSP 11,4 % de ello) | **18,9 %** | 18,9 % |
| código generado | 17,3 % | 99,5 % |
| AICA ARM7 | 7,8 % | 10,0 % |
| scheduler | 3,4 % | 33,7 % |
| AICA otro | 2,3 % | 2,4 % |
| PVR/TA (`Ta::feed`) | 1,4 % | 1,4 % |
| llamadas indirectas | 0,8 % | 0,8 % |
| excepciones/unwind | 0,7 % | 0,7 % |

Top self: `read32` 15,0 %, `dsp_step` 11,4 %, `store<u32>` 11,0 %, `sq_flush` 7,6 % (12,8 % incl.), `Arm7::step` 6,9 %,
`write32` 4,6 %, `Mixer::sample` 4,4 %, `njCnkEasyVertexVN` 3,9 %, `advance_to` 3,0 %, `stream_step` 3,0 %, `read16`
2,2 %, `Aica::aram` 2,1 %. `System::on_idle` 0,5 % (2,3 % incl.).

Contadores del gameplay (diferencia entre `rep-i40.txt` y `rep-i80.txt`, CONFIRMADO):

| contador | 40–80 s | por segundo de guest | por render (2 398 renders) |
|---|---:|---:|---:|
| ráfagas de store queue (todas por la UTLB, 0 fallos) | 36 814 361 | **920 000** | 15 350 |
| trozos de 32 B al TA | 29 273 277 | 732 000 | 12 200 |
| muestras AICA | 1 764 057 | 44 100 | — |
| interrupciones entregadas | 19 178 | 479 | — |
| idle skips | 2 330 350 | 58 000 | — |

Cada idle-skip salta ≈2 160 ciclos SH-4 (10 696 263 050 / 4 951 417, CONFIRMADO en el reporte): el salto se corta en el
siguiente evento del scheduler, que casi siempre es la muestra AICA (cada 4 535 ciclos,
`dr/runtime/include/dream/runtime/aica/aica.h:25`) o la línea del SPG (un evento por línea,
`dr/runtime/src/pvr/spg.cpp:37` y `:72`).

## 2. Optimizaciones y mejoras

Formato de cada ítem: **qué cambia** (archivo:línea que abrí) · **mecanismo** · **ganancia** [INFER] con su
razonamiento y la medición de la que sale · **cómo medirla** · **riesgo** · **esfuerzo** (bajo ≤1 día, medio 2–5
días, alto >1 semana). Ordenados por ganancia/esfuerzo dentro de cada grupo.

Hechos de partida que uso en varios ítems (todos CONFIRMADOS en esta revisión):

- El código generado recibe `Memory&` abstracto y llama `m.read32`/`m.write32` por la vtable
  (`dr/runtime/include/dream/runtime/memory.h:16-30`; `gen/ikaruga.cpp` incluye ese encabezado y hace `using
  dream::Memory`). Sitios en el C++ generado: 54 711 `m.read32`, 36 494 `m.write32`, 72 520 `c.cycles +=`, 6 841
  `c.cycles >= c.next_event`, 1 051 `call_indirect`, 1 204 `fmac(` (`grep -c` sobre `gen/ikaruga.cpp`).
  `elf-Os` tiene 114 356 `jalr`.
- `Ctx::cycles`/`next_event` son `uint64_t` (`dr/runtime/include/dream/runtime/sh4/ctx.h:38-42`). En `elf-Os`, la
  comprobación de entrada de `njCnkEasyVertexVN__resume` son 7 instrucciones con dos `lw` por lado (0x0914bf24…
  0x0914bf88). `Ctx::fr` queda en el desplazamiento 152 (8 mod 16), porque `cycles` cae en 280 (los `lw …,280/284`
  del mismo desensamblado).
- En `elf-Os` hay 3 338 llamadas a `__muldf3`, 2 933 a `__adddf3`, 2 190 a `__subdf3`, 924 a `__floatsidf`, 231 a
  `__divdf3` y 1 202 a `fmaf` (conteo de `jal` en `psp-objdump -d`). Casi todas viven en las ramas `FPSCR.PR=1` que el
  traductor emite porque no sabe el modo (49 862 «fp runtime branches», SEGÚN `medicion-pc.md` §3.1).

### 2.A Antes de tener video

**A1. Runtime y stubs a `-O2`; generado como hoy.**
- Qué cambia: `repo/psp/ikaruga/Makefile:8` (`OPT`) y `:38` (`CFLAGS = $(OPT) …`): una variable para el generado
  (`obj/gen/%.o`, `:54-56`) y `-O2` fijo para `obj/rt/%.o` (`:46-48`) y `obj/host/%.o` (`:58-64`).
- Mecanismo: a `-Os`, `DcMemory::read32` hace `jal resolve` (que devuelve un `Target` de 16 B por la pila) y `jal memcpy`
  de 4 bytes (CONFIRMADO `elf-Os` 0x880a54c, 0x880a56c; `memcpy` de newlib entra por la rama de <8 bytes). A `-O2`
  el `memcpy` se vuelve `lwl/lwr` (CONFIRMADO `elf-O2`, `read32` en 0x0880cc6c); `resolve` sigue fuera de línea.
- Ganancia: [INFER] 10–25 % del total en PPSSPP. Razonamiento: el camino de RAM de `read32` a `-Os` son ≈60–80
  instrucciones (vtable, CCN, marco, `resolve` ≈40, `memcpy` ≈15); quitar `memcpy` y el marco ahorra ≈20–25, y
  `DcMemory` es el 42 % del gameplay en PC (§1.4). Tamaño: +85 KB (SEGÚN `tamano-psp.md` §2).
- Cómo medir: `scripts/probar-ikaruga-ppsspp.sh … 300` antes y después; mismos ciclos y PC del guest en los cuadros
  60/180 (prueba de `arranque-psp.md`), menos `host_s`.
- Riesgo: ninguno conocido. Esfuerzo: bajo.

**A2. Arreglar el redondeo en MIPS (bug).**
- Qué cambia: `repo/patches/dream-recomp/0001-fenv-mips-fcsr.patch`: la rama MIPS de `fenv.cpp` debe escribir el
  campo RM del FCSR (bits 1:0; 0 = al más cercano, 1 = a cero) con `ctc1 $31`, igual que ya hace con FS (bit 24), y
  `current()` leerlo con `cfc1`; hoy `apply` llama `std::fesetround` (`dr/runtime/src/fenv.cpp:72-75`), que en
  newlib-psp no hace nada (CONFIRMADO, `elf-Os` 0x0940e178). Corregir la línea del `README.md` de los parches que dice
  que el redondeo «keeps going through `<cfenv>`».
- Mecanismo: el guest corre con RM = a cero (FPSCR inicial `0x00040001`); sin esto cada `fadd/fmul/fdiv/ftrc`
  redondea distinto que en el Dreamcast y que en el PC.
- Ganancia: corrección, no velocidad. Sin ella cualquier comparación bit a bit con el PC (A13, T1) diverge en cuanto
  el juego hace aritmética de punto flotante; y puede haber diferencias de juego (posiciones, colisiones) [INFER].
- Cómo medir: microprueba en el EBOOT (`1.0f/3.0f` con RM a cero y al más cercano, comparar bits) y la prueba de
  hash de escrituras T1. Ojo: el soft-float de libgcc no lee el FCSR (redondea siempre al más cercano) [INFER], así que
  FIPR/FTRV/FMAC seguirán distintos hasta A3.
- Riesgo: que el Allegrex no implemente RM=1 igual que MIPS32 [INFER; a confirmar con la microprueba en hardware;
  PPSSPP puede no modelarlo]. Esfuerzo: bajo.

**A3. FIPR, FTRV y FMAC sin `double` (VFPU o `float`).**
- Qué cambia: `dr/runtime/include/dream/runtime/sh4/ops.h:315-318` (`fmac` → `std::fmaf`), `:340-349` (`fipr`),
  `:352-363` (`ftrv`), con un parche PSP en `repo/patches/dream-recomp/`.
- Mecanismo: hoy cada FIPR son 16 llamadas de soft-float y cada FTRV 64 (CONFIRMADO §0.1), y `fmaf` 6. Tres niveles:
  (a) VFPU: `vdot.q` / `vtfm4.q` con `ulv.q`/`usv.q` (o copia a un búfer alineado) porque `fr` y `xf` quedan a 8 mod 16
  (ver hechos de partida); el hilo ya tiene `PSP_THREAD_ATTR_VFPU` (CONFIRMADO `repo/psp/ikaruga/main.cpp:35`). (b) C
  en `float` con la FPU. (c) Emulación exacta en `float` (productos exactos por división de Veltkamp/Dekker, suma con
  TwoSum, redondeo final según RM): ≈100–150 operaciones de FPU, sigue siendo 10–30× más barata que el soft-float [INFER].
  `fmac`: `a*b+c` en `float` (dos redondeos) o la versión exacta con TwoProduct.
- Ganancia: [INFER] probablemente la mayor de toda la lista para el gameplay. Razonamiento: una operación `double` de
  libgcc ronda 50–150 instrucciones en MIPS32 sin FPU doble; FTRV ≈ 3 000–6 000 instrucciones, FIPR ≈ 700–1 500, contra
  ≈20–40 con VFPU. Ikaruga hace ≈1,6 M llamadas a funciones Ninja con FTRV en 80 s (SEGÚN `medicion-pc.md` §3.2) y
  `njCnk*Vertex*` las ejecuta por vértice; con 12 200 trozos de TA por render (§1.4), unos pocos miles de vértices
  transformados por cuadro ya serían cientos de millones de instrucciones por segundo. No está medido cuántas FTRV/FIPR
  ejecuta (ver «cómo medir»).
- Cómo medir: contadores de FIPR/FTRV/FMAC en el EBOOT (A0) × costo por llamada medido con una microprueba en PPSSPP
  (que cuenta instrucciones) y luego en hardware.
- Riesgo: (a) y (b) cambian el último bit respecto del PC (el runtime eligió la acumulación en `double` para ser
  idéntico a Flycast, comentario en `ops.h:336-339`); si solo afecta vértices dibujados no se nota, si alimenta
  colisiones (p. ej. `njCalcPoint*`) el juego puede divergir del PC. Mitigación: interruptor de compilación y la prueba
  T1 para ver desde qué cuadro diverge. (c) es exacta pero hay que demostrarlo (prueba contra la versión `double` con
  millones de entradas aleatorias en PC). Esfuerzo: (b) bajo, (a) bajo-medio, (c) medio.

**A4. Camino rápido de RAM sin tocar el traductor: un `memory.h` propio para la PSP.**
- Qué cambia: un parche PSP de `dr/runtime/include/dream/runtime/memory.h:16-30`: `Memory` gana un puntero `ram_` y
  `read8/16/32`, `write8/16/32` no virtuales `inline` que, si la dirección es del área 3 y menor que `0xE0000000`,
  leen/escriben `ram_ + (a & 0x00FFFFFF)`; si no, llaman a los virtuales lentos (`read32_slow`…), que `DcMemory`
  implementa con su código de hoy (`dc_memory.cpp:296-…`). El C++ generado se recompila igual: solo usa los nombres.
- Mecanismo: quita por acceso la vtable (2 `lw` + `jalr`), la comprobación de CCN, `resolve` (la comprobación P4, OCRAM y
  el `switch` de área, `dc_memory.cpp:149-226`), el `Target` por la pila y la copia. ≈8 instrucciones contra 40–80.
  `store<T>` además deja de evaluar `journaling` y `note_ram_write` (`dc_memory.cpp:247-268`, `dc_memory.h:195-200`).
- Ganancia: [INFER] 1,4–2× en total. Razonamiento: `DcMemory` 42 % self con idle-skip (§1.4); `read32`+`store`+`write32`
  +`read16` son 33 de esos 42 puntos y casi todos son RAM (la pila `r15` en particular: el código generado pasa todos
  los `mov.l @-r15`/`@(n,r15)` por `m.write32`/`m.read32`, CONFIRMADO en `kmWaitVBlank__resume`). El estudio de
  dream-recomp midió 3,5× → 5,8× con lo mismo en un M4 (SEGÚN `dr/docs/cpu-performance-study.md` §0).
- Tamaño: ≈91 000 sitios de lectura/escritura de 32 bits más los de 8/16; `inline` en todos son ≈+2–3 MB [INFER:
  6–8 instrucciones por sitio]. Variante sin costo de tamaño: el camino rápido como función no virtual fuera de línea
  (un `jal` directo), que ya quita vtable, `resolve` y copia; `inline` solo en las funciones calientes (A10).
- Cómo medir: PPSSPP antes/después con la prueba de cuadros 60/180 (mismos ciclos y PC) y el hash T1 idéntico.
- Riesgo: P4 `0xEC000000` también tiene `(a & 0x1C000000) == 0x0C000000`; hay que excluir `a >= 0xE0000000`.
  Las ayudas de desarrollo (`hash_writes`, `watch`, `journal`) dejan de ver las escrituras rápidas: compilar el
  camino rápido con un interruptor para poder correr T1 sin él. Esfuerzo: bajo-medio.

**A5. Store queues y TA: el camino de las ráfagas.**
- Qué cambia: `dr/runtime/src/mem/dc_memory.cpp:388-390` (`sq_write32`), `:397-434` (`sq_flush`), `:89-102`
  (`Utlb::translate`, búsqueda lineal de 64), `dr/runtime/src/pvr/core.cpp:209-218` (`write_burst`), `:54-56`
  (`Ta::feed`: `stream.insert`), `dr/runtime/src/system.cpp:46-52` (`on_device_access`).
- Mecanismo: por cada una de las 920 000 ráfagas/s del gameplay (§1.4): (1) recordar la última traducción de la UTLB
  (base/máscara de página) y probarla antes de recorrer las 64 entradas; invalidarla cuando el juego escribe la UTLB;
  (2) si el destino es la FIFO del TA, ir directo a `Core::write_burst` sin `resolve` ni la búsqueda lineal de MMIO;
  (3) `on_device_access` (un `std::function` que llama a `advance_to`, que recorre los ≈12 eventos con comparaciones de
  64 bits) no hace falta para el TA, que no depende del reloj; o, en general, salir si `ctx.cycles < ctx.next_event`;
  (4) `stream.reserve` del tamaño del render anterior; (5) `sq_write32` por el camino rápido de A4 (el área SQ es un
  arreglo).
- Ganancia: [INFER] grande. `sq_flush` es 12,8 % inclusivo en PC con idle-skip; en la PSP cada ráfaga cuesta hoy
  ≈300–500 instrucciones (64 vueltas de UTLB, dos búsquedas lineales, `std::function`, `vector::insert`) → 280–460 M
  instrucciones/s, más que la CPU entera; tras el cambio ≈60–100.
- Cómo medir: contador de ráfagas y tiempo acumulado en `sq_flush` (`sceKernelGetSystemTimeLow` alrededor, solo en un
  build de medición) en PPSSPP; mismas cifras de TA en el reporte.
- Riesgo: bajo si la caché de UTLB se invalida en toda escritura a la UTLB y a MMUCR. Esfuerzo: bajo-medio.

**A6. Scheduler: menos eventos.**
- Qué cambia: `dr/runtime/src/pvr/spg.cpp:40-72` (`on_line`, un evento por línea: 525 × 60 = 31 500/s);
  `dr/runtime/src/aica/aica.cpp:14-18` (un evento por muestra: 44 100/s); `dr/runtime/src/sched/scheduler.cpp:17-44`
  (búsqueda lineal en `next_deadline` y en cada vuelta de `advance_to`; `Event` lleva un `std::string`).
- Mecanismo: (a) SPG: programar solo las líneas donde pasa algo (`vblank_in`, `vblank_out`, `vstart`, `vbend`, la de
  `hblank_int` en modo 0, la 0) y calcular `scanline_` desde los ciclos cuando el juego lee `SPG_STATUS`; en modo 2
  (HBlank por línea) dejar el camino actual. (b) AICA en lotes: un evento cada N muestras que corre el ARM7 N×512 ciclos
  y N muestras del mezclador. El acoplamiento SH-4↔ARM es solo por ARAM, sin interrupciones en ningún sentido (§1.1
  fila 14), así que el lote cambia la latencia de esa comunicación, no la semántica [INFER]. (c) `next_deadline`
  guardado y callbacks como punteros a función.
- Ganancia: [INFER] 7–15 % de la CPU de la PSP. Razonamiento: ≈76 000 eventos/s; cada uno sale del código traducido
  por `deliver_irq` (hook virtual) → `on_poll` → `advance_to` (≈12 comparaciones de 64 bits) → `std::function` →
  `arm_next_poll` (otra búsqueda): ≈300–600 instrucciones en MIPS32. Además alarga los saltos del idle-skip, que hoy
  se cortan cada ≈2 160 ciclos (§1.4) y obligan a ejecutar dos vueltas reales del bucle por salto.
- Cómo medir: contador de eventos/s y el reporte de idle-skip (`idle skips: N (T loop turns, C cycles)`): T/N debe
  subir; ciclos y PC en los cuadros 60/180 sin cambios para (a) (comparar también en PC).
- Riesgo: (a) ninguno si `SPG_STATUS` se calcula igual; probar en PC que los ciclos y el PC final de la corrida de 80 s
  no cambian. (b) cambia la temporización del audio; Ikaruga puede sondear posiciones de canal desde el ARM y ver
  saltos más grandes [INFER]. Esfuerzo: (a) bajo, (b) medio.

**A7. AICA en modo reducido mientras no haya audio, y como opción después.**
- Qué cambia: `dr/runtime/src/aica/mixer.cpp:903-1028` (`dsp_step`, 128 pasos por muestra con producto de 64 bits),
  `:1030-1062` (`sample`: 64 canales por muestra).
- Mecanismo: (a) DSP apagado por interruptor (`dsp_.stopped = true`): el DSP solo produce los efectos (`EFREG`) que se
  suman a la salida (`mixer.cpp:1040-1044`); (b) sin salida de audio, avanzar la posición de cada canal (lo que el
  driver del ARM puede leer) sin mezclar volumen/pan/filtro; (c) mezclar a 22 050 Hz.
- Ganancia: [INFER] de 29 % a ≈10 % del gameplay en PC; en la PSP la proporción del DSP sube porque cada paso usa
  productos de 64 bits y desplazamientos en registros de 32 [INFER]. Razonamiento: `dsp_step` 11,4 % y `Mixer::sample`
  + `stream_step` 7,4 % self (§1.4).
- Cómo medir: perfil de PC con el interruptor (es un parche del runtime, igual en PC y PSP) y contador en el EBOOT.
- Riesgo: (a) sin reverberación; que el ARM lea `EFREG`/`MEMS` es improbable [INFER, no lo verifiqué]. (b) si el driver
  sondea registros de canal que dependen de la mezcla, cambia el comportamiento del sonido, no del SH-4 (0 interrupciones
  ARM→SH-4) [INFER]. Esfuerzo: bajo.

**A8. Idle-skip en la PSP (revisión del parche de la fila 5d).**
- Qué cambia: `patches/dream-recomp/0004-idle-skip-runtime.patch` del worktree de 5d (líneas 121-165 del parche,
  `System::on_idle`).
- Mecanismo: en cada sondeo copia el `Ctx` entero (296 B) a un estático y hace `memcmp` de dos copias; busca la cabeza
  en un `std::map`; y en cada salto divide 64 bits (`(c.next_event - c.cycles - 1) / d`), que en MIPS32 es
  `__udivdi3`. Con 58 000 saltos/s (§1.4) eso son ≈10–20 M instrucciones/s solo en la división [INFER]. Cambiarla a 32
  bits (la distancia al siguiente evento y `d` caben en 32 bits; acotar a `UINT32_MAX`), y la tabla a un arreglo
  pequeño. Lo demás del diseño (comparación completa del `Ctx`, el timeout en `@@r15`) está bien y es seguro.
- Ganancia: [INFER] 3–6 % de la CPU de la PSP, menos si A6 alarga los saltos.
- Cómo medir: el mismo `idle skips:` del reporte y `host_s` en PPSSPP.
- Riesgo: ninguno si se acota. Esfuerzo: bajo.

**A9. Instrumentación en el EBOOT (prerrequisito de medir A3–A8).**
- Qué cambia: `repo/psp/ikaruga/main.cpp:158-171` (la línea `tick`) y un parche de medición del runtime.
- Mecanismo: contadores de FIPR/FTRV/FMAC, lecturas/escrituras por tipo de destino, ráfagas SQ, eventos del scheduler,
  `throw`s; y tiempo por fase (código traducido / `on_poll` / AICA / `sq_flush` / dibujo) con `sceKernelGetSystemTimeLow`
  en el build de medición. Una microprueba aparte (otro EBOOT) mide el costo unitario de `fipr`, `ftrv`, `fmaf`,
  `read32`, `sq_flush` y un `throw NonLocalReturn` a través de 3 marcos.
- Ganancia: ninguna directa; convierte los [INFER] de A3–A8 y A12 en cifras. Riesgo: que el contador altere lo medido
  (mantenerlo fuera del build normal). Esfuerzo: bajo.

**A10. `-O2` solo en lo caliente.**
- Qué cambia: `repo/psp/ikaruga/Makefile:54-56` y un paso del script de build que marque las funciones calientes.
- Mecanismo: el generado es una sola unidad de 45 MB con funciones `static` (`__resume`) que se llaman entre sí, así que
  no se puede partir en dos archivos sin cambiar el traductor; sí se puede anteponer
  `__attribute__((optimize("O2")))` (o `#pragma GCC push_options/optimize`) a las definiciones de la lista caliente,
  sacada de la cobertura (`medicion-pc.md` §4: 1 452 funciones en 0–80 s) ampliada con los capítulos 2–5.
- Ganancia: [INFER] desconocida en MIPS; `-O2` frente a `-Os` suele dar 10–30 % en código con bucles, menos en código
  de una sola pasada como este. Tamaño: generado `-O2` 22,39 MB contra `-Os` 11,47 MB (SEGÚN `tamano-psp.md` §2); con 20 %
  de los bytes calientes ≈+2,2 MB, con 30 % ≈+3,3 MB → cabe (margen de 6–8 MB).
- Cómo medir: PPSSPP con el mismo arranque; tamaño con `psp-size`.
- Riesgo: el atributo `optimize` es de uso limitado según la documentación de GCC (no hereda todas las banderas);
  comprobar con `-Q --help=optimizers` qué queda activo. Esfuerzo: medio.

**A11. Contador de ciclos de 32 bits en el código emitido.**
- Qué cambia: el traductor (como el parche de idle-skip, `repo/patches/dream-recomp-pc/0001-…`) y
  `dr/runtime/include/dream/runtime/sh4/ctx.h:38-42`.
- Mecanismo: el emisor descuenta un presupuesto de 32 bits (`c.budget -= k; if ((int32_t)c.budget <= 0) …`) y el
  runtime reconstruye `cycles` en `deliver_irq`. En MIPS32 cada `c.cycles += k` de 64 bits son ≈7 instrucciones (dos
  `lw`, suma, acarreo, dos `sw`) y cada comprobación otras 7 (CONFIRMADO en la entrada de `njCnkEasyVertexVN__resume`).
- Ganancia: [INFER] 5–10 % de las instrucciones del código generado (72 520 sumas, una por bloque).
- Cómo medir: tamaño del `.text` del generado y PPSSPP. Riesgo: el idle-skip y todo lo que lee `ctx.cycles` debe
  pasar por la reconstrucción. Esfuerzo: medio (traductor + runtime + regenerar).

**A12. Quitar las ramas `FPSCR.PR` del código emitido.**
- Qué cambia: el traductor (emisor de operaciones FP) con una opción por juego: PR=0 en todas las funciones salvo una
  lista (las 5 de la libm de doble, SEGÚN `medicion-pc.md` §3.1); `write_fpscr` (`ops.h:365-376`) falla en voz alta si
  aparece PR=1 fuera de esa lista.
- Mecanismo: elimina ≈12 400 ramas PR (las de SZ se quedan: hay 576 `fschg`) y el código soft-float muerto.
- Ganancia: [INFER] tamaño 0,5–1,5 MB (las ≈9 600 llamadas a `__*df3`/`__floatsidf` con su preparación de argumentos) y
  ≈3 instrucciones por operación FP. El margen de memoria liberado paga parte de A4 y A10.
- Riesgo: un `lds` que restaura un FPSCR guardado con PR=1 (`longjmp`, ISR; `medicion-pc.md` no lo instrumentó): la
  comprobación en `write_fpscr` lo detecta. Esfuerzo: medio.

**A13. Excepciones C++ para los retornos no locales: medir antes de tocar.**
- Qué cambia (si hace falta): `dr/runtime/src/sh4/abi_bare.cpp` (`nonlocal_return`, `run_guest_impl`).
- Mecanismo: 204 `throw`/s (§1.1 fila 12). En el Allegrex cada `throw` recorre `.eh_frame` (0,55 MB) con el desenrollador
  DWARF de libgcc en dos fases [INFER: 10 000–100 000 ciclos] → 0,6–6 % de la CPU. Alternativa si pesa:
  `setjmp/longjmp` al marco de `run_guest_impl` (no hay destructores en el código generado: `.gcc_except_table` del
  generado a `-Os` es 0, SEGÚN `tamano-psp.md` §2), lo que además permitiría compilar el generado con
  `-fno-asynchronous-unwind-tables` y ahorrar ≈0,4 MB.
- Cómo medir: A9 (microprueba y contador). Riesgo: medio (cambia el modelo de retornos no locales). Esfuerzo: medio.

**A14. Higiene de build.**
- Qué cambia: `repo/psp/ikaruga/Makefile:38-40`: añadir `-ffunction-sections -fdata-sections` y `-Wl,--gc-sections`,
  `-fno-math-errno`, `-mno-check-zero-division` (las usaba `animal_forest_psp` en su Makefile, SEGÚN lectura previa a
  que se borrara).
- Ganancia: [INFER] 0,3–1 MB (libstdc++ pesa 1 MB, SEGÚN `tamano-psp.md` §2) y algo de velocidad en división y `sqrtf`.
- Riesgo: `--gc-sections` puede quitar la tabla de funciones registrada por inicializadores estáticos si no se
  referencia (comprobar `untranslated call targets: 0`). Esfuerzo: bajo.

### 2.B Después de tener video (revisión del diseño de la fila 5c)

Leí el diff sin commitear del worktree de 5c (`.git-worktrees/2km21h`, `psp/stubs/video.cpp`, `psp/ikaruga/main.cpp`).

**B1. El GE en paralelo con la CPU.** `video_draw_frame` construye la lista, hace `sceGuFinish()` y luego
`sceGuSync(GU_SYNC_FINISH, …)`: la CPU espera a que el GE termine cada render. Con dos listas y dos búferes de vértices,
sincronizar justo antes de reutilizar el par anterior deja que el GE dibuje el render N mientras la CPU emula el N+1.
Ganancia [INFER]: todo el tiempo de GE por cuadro (sin medir). Riesgo: bajo. Esfuerzo: bajo.

**B2. Framebuffers de 16 bits.** Hoy dos búferes RGBA8888 de 512×272 (544 KB cada uno) + profundidad de 16 bits (272
KB) = 1,36 MB de los 2 MB de eDRAM. Con `GU_PSM_5650` y `sceGuEnable(GU_DITHER)`: 272 KB por búfer, 816 KB en total,
≈1,2 MB libres para texturas en eDRAM en lugar de ≈0,6 MB; además la mitad de ancho de banda de relleno. Riesgo:
bandas de color en degradados [INFER: Ikaruga es mayormente de colores planos]. Esfuerzo: bajo.

**B3. Cambios de estado solo cuando cambian.** El dibujo emite `sceGuDepthFunc`, `sceGuDepthMask`, blend y alpha test
por cada tira. Guardar el último estado emitido y omitir los repetidos reduce la lista y la escritura de la CPU.
Ganancia [INFER] pequeña-media con ≈12 000 trozos de TA por render. Esfuerzo: bajo.

**B4. Lista de comandos acotada.** `g_list` es un arreglo estático de 1 MB sin comprobación de desbordamiento (sceGu no
comprueba). Un render con muchas tiras corrompe memoria en silencio. Comprobar el espacio usado
(`sceGuCheckList`) y partir el render o descartar el resto con un aviso. Riesgo actual: alto (bug latente). Esfuerzo:
bajo.

**B5. Decodificar el TA una sola vez.** Hoy el TA se copia entero a `ta.stream` (`core.cpp:56`, ≈390 KB por render a
12 200 trozos) y el video lo vuelve a recorrer con `DisplayList::feed_stream`. Decodificar al vuelo (o convertir
directamente a vértices del GE) ahorra la copia y una pasada. Ganancia [INFER] media. Esfuerzo: medio (toca
`dream::render`).

**B6. Texturas.** Decodificar a 16 bits (4444/5551/5650) o mantener paleta (CLUT del GE) cuando el origen es de paleta;
VQ no existe en el GE y se decodifica; swizzle al subir; caché por dirección+palabra de control+hash, residente en eDRAM
con LRU y respaldo en RAM. El juego sube poco: 3,6 MB de texturas por DMA en 80 s (CONFIRMADO, reporte `ch2 DMA`), así
que la caché casi no se renueva. Diseño SEGÚN la pieza 3 del diseño (`docs/plan.md`, paso «Texturas a 16 bits») y `oot-psp` `psp_texture_manager.c:221,335`.

**B7. Vértices en coordenadas de 16 bits** (`GU_VERTEX_16BIT` en modo 2D): mitad de memoria y de ancho de banda que
`GU_VERTEX_32BITF`. Riesgo: precisión subpíxel (sin importancia a 480×272) [INFER]. Esfuerzo: bajo.

**B8. Saltar el dibujo de algunos cuadros** (no la lógica): con la emulación por debajo de tiempo real, decodificar y
dibujar uno de cada N renders ahorra B5+GE. La rotación TATE (diseño §4) cuesta cero en la conversión de vértices.

**B9. Audio y Media Engine.** Lo que `shar-psp-engine` midió en hardware (SEGÚN `dream-recomp:docs/research/media-engine.md`): el
ME no tiene VFPU ni E/S, su caché no es coherente con la del núcleo principal (toda entrega exige *flush* de un lado y
dirección sin caché `+0x40000000` del otro, `:18`), PPSSPP no ejecuta código del ME (`:21`), y tareas chicas no ganan
(partículas y culling, `:41-42`). Para el AICA en el ME eso implica: ARAM compartida sin caché en uno de los dos lados
(el SH-4 casi no la toca en gameplay: lo que hace el ARM sí [INFER]), el ARM7 y el mezclador corriendo en el ME en
lotes (A6 b), y un respaldo en el núcleo principal si el ME no responde (`shar` usa 50 ms, `:46`). Medible solo en
hardware.

**B10. Disposición del código caliente.** Con `-ffunction-sections` (A14) y una lista de orden para el enlazador, juntar
las funciones calientes (Ninja, Kamui, `DcMemory`, scheduler) reduce fallos de la caché de instrucciones de 16 KB. Medible
solo en hardware. Esfuerzo: bajo una vez que A14 y A10 estén.

**B11. Scratchpad de 16 KB.** Candidatos: estado del DSP (`TEMP`/`MEMS`) si el AICA sigue en el núcleo principal, o
funciones calientes copiadas ahí (`animal_forest_psp` corría así funciones de su renderizador, SEGÚN lectura previa).
El `Ctx` (296 B) ya debería vivir en la caché de datos [INFER]. Ganancia baja; solo tras medir en hardware.

### 2.C Bugs, riesgos y deuda en nuestro código

| # | Dónde | Qué | Severidad |
|---|---|---|---|
| C1 | `repo/patches/dream-recomp/0001-fenv-mips-fcsr.patch` + `README.md` | `fesetround` no-op: el guest corre con redondeo al más cercano (A2) | Alta (corrección) |
| C2 | `repo/psp/ikaruga/Makefile:8,38` | runtime a `-Os` con `memcpy` por acceso (A1) | Alta (velocidad) |
| C3 | `repo/psp/ikaruga/main.cpp` | Sin callback de salida (`sceKernelRegisterExitCallback`; `repo/psp/hola/main.c:21` sí lo tiene): HOME → salir no termina limpio | Media |
| C4 | `repo/psp/stubs/disc.cpp:26` | Sin callback de energía: tras suspender la PSP los descriptores de `ms0:` dejan de valer y el disco no se reabre. `shar-psp-engine` prueba exactamente eso (`tests/audio/test_suspend_resume_all.c:88-96`) | Media |
| C5 | `repo/psp/ikaruga/main.cpp:45` | `kDir` fijo `ms0:/PSP/GAME/IKARUGA/`: no funciona en la memoria interna de la PSP Go (`ef0:`) ni si se renombra la carpeta. `shar-psp-engine` la saca de `argv[0]` (`metest/main.c:303`) | Baja-media |
| C6 | `repo/psp/ikaruga/main.cpp:153-154` | RTC sembrado con `time(nullptr)`: el PC se compara con `--rtc-seed 1`; tras el arranque las corridas pueden divergir del PC por la hora. Hace falta una opción de semilla fija para las pruebas | Media (pruebas) |
| C7 | `repo/psp/stubs/vmu.cpp` | La VMU se carga y nunca se guarda. Al escribirla: temporal + `sceIoRename` + `sceIoSync("ms0:",0)` como `shar-psp-engine/tests/loadbench/main.c:238` | Media (funcional) |
| C8 | `repo/psp/stubs/disc.cpp:60-78` | Una llamada `sceIoLseek`+`sceIoRead` por sector de 2 048 B; en hardware cada lectura chica de la Memory Stick cuesta milisegundos [INFER]. Un búfer de lectura adelantada de 32–64 KB | Baja (tiempos de carga) |
| C9 | `repo/psp/ikaruga/main.cpp:39` | `PSP_HEAP_SIZE_KB(-1024)` deja 1 MB fuera del heap; las pilas de hilos nuevos (audio, video) salen de esa memoria [INFER]: con hilos de 64–256 KB y lo que reserve el kernel puede no alcanzar | Baja-media |
| C10 | `repo/psp/stubs/file.cpp:43-50` | El log abre, escribe y cierra en cada línea; en hardware eso altera `host_s` de la medición [INFER] | Baja |
| C11 | worktree 5c, `psp/stubs/video.cpp` | `g_list` de 1 MB sin control de desbordamiento (B4); `sceGuSync` bloqueante (B1) | Media |
| C12 | worktree 5d, `0004-idle-skip-runtime.patch` | División de 64 bits por salto (A8) | Baja |
| C13 | `third_party` + `patches/` | El parche de fenv cambia FS (bit 24) sin verificar que el Allegrex lo respete (SEGÚN `tamano-psp.md`, «Qué NO verifiqué») | Baja |

### 2.D Pruebas y calidad del port

**T1. Oráculo de determinismo PC↔PSP por hash de escrituras.** `DcMemory` ya trae `hash_writes`/`write_hash`
(`dr/runtime/include/dream/runtime/mem/dc_memory.h:138-143`). Imprimir el hash cada 60 cuadros en PC y en el EBOOT
(con RTC fijo, C6) y comparar: el primer cuadro distinto localiza la divergencia que introduzcan A2/A3/A4. Es la prueba
de aceptación natural para todas las optimizaciones de CPU. Esfuerzo: bajo.

**T2. Ampliar `scripts/test-psp-arranque.sh`** a los cuadros 60/180/600 con RTC fijo y exigir ciclos idénticos al PC
(hoy difieren < 0,07 % porque la medición se toma en instantes distintos, `arranque-psp.md`); con el hash T1 eso queda
exacto.

**T3. Microbenchmark de costos unitarios** (A9) en PPSSPP y en hardware.

**T4. Prueba en hardware (PSP-3001).** PPSSPP no modela caché, latencia de memoria ni el ME; las decisiones B9–B11 y la
cifra final de velocidad solo valen medidas en la consola.

**T5. Controles.** `shar-psp-engine` lee con `sceCtrlPeekBufferPositive` tras `sceCtrlSetSamplingCycle(0)` y modo
digital (`metest/main.c:304`, `:232`); Ikaruga solo necesita la cruceta y botones (sin analógico) [INFER], así que el
modo digital basta y evita deriva del stick; el remapeo TATE (diseño §4) va en la tabla de botones.

### 2.E Lo que no vale la pena (y por qué)

| Idea | Por qué no |
|---|---|
| Overlays de `ikaruga_reloc_*` | 2,5–5,7 KB de C++ (§1.1 fila 15) |
| Overlays/PRX del código frío, hoy | `-Os` cabe con 6–8 MB de margen; un cargador de overlays con retornos no locales por excepción y llamadas indirectas por tabla es mucho riesgo para memoria que no falta. Revisar solo si video+texturas+audio se comen el margen |
| Más trabajo en llamadas indirectas | 0,8 % con idle-skip (§1.4) |
| Reducir RAM (16 MB) o VRAM (8 MB) del guest | El juego direcciona todo (diseño §2) |
| Quitar la Boot ROM sin más | Ahorra 2 MB, pero si algún día se quiere la fuente de la BIOS para los mensajes de la VMU hace falta su región; alternativa: cargar solo la región de la fuente. Solo si falta memoria |
| Mover tareas chicas al ME | `shar-psp-engine` no ganó nada con partículas ni culling (`media-engine.md:41-42`); el ME solo para el AICA completo |
| `-O3` o LTO sobre la unidad de 45 MB | `cc1plus` ya tarda 311 s a `-O2` y `psp-as` es superlineal (SEGÚN `tamano-psp.md` §4); el tamaño crece y no cabe |
| Afinar con PPSSPP la caché y la memoria | No las modela (`arranque-psp.md`, advertencia) |
| Scratchpad para el `Ctx` | Ya está en la caché de datos [INFER] |

### 2.F Orden recomendado

Antes de video, por ganancia/esfuerzo: **A1** (minutos) → **A2** (bug) → **A9** (medir) → **A3** (FPU sin `double`)
→ **A4** (RAM rápida) → **A5** (SQ/TA) → **A6 a** (SPG) → **A8** (idle-skip PSP) → **A7** (AICA reducido) → **A6 b**
(AICA en lotes) → **A14** → **A10**/**A11**/**A12** (tocan el traductor o el build del generado) → **A13** si A9 dice que
pesa. Con T1 como prueba de aceptación de cada una.
Después de video: **B4** (bug) → **B1** → **B2** → **B3** → **B6** → **B5** → **B7/B8** → **B9** (hardware) →
**B10/B11** (hardware).

## Qué NO verifiqué y dónde está mi punto ciego

- **Nada corrió en una PSP real.** Todas las ganancias de §2 son [INFER]; las cifras de instrucciones por operación
  salen de leer el desensamblado, no de contar ciclos. El Allegrex tiene latencias de FPU, de `mult` y de caché que
  PPSSPP no modela.
- **No medí cuántas FIPR/FTRV/FMAC ejecuta Ikaruga**, solo cuántas llamadas a funciones Ninja que las contienen
  (`medicion-pc.md`). La afirmación de que el soft-float domina el gameplay en la PSP es la más importante del documento y
  la menos medida: depende de ese conteo (A9). Tampoco medí el costo real de `__muldf3`/`__adddf3` en el Allegrex.
- **Que el soft-float de libgcc ignore el FCSR** (y por tanto redondee al más cercano aunque A2 se arregle) es [INFER]
  sobre cómo está construida la libgcc del toolchain; no abrí su fuente.
- **Que el Allegrex implemente RM=1 (redondeo a cero) y FS (bit 24)** como MIPS32: no verificado; tampoco si PPSSPP lo
  emula. La microprueba de A2 debe correr en hardware.
- **El perfil con idle-skip es de PC** (Ryzen 9950X, muestreador por `SIGPROF` + `backtrace`, 6 096 muestras, ±0,6 pp en
  las categorías grandes); hereda los límites de `medicion-pc.md` §«Qué NO verifiqué» (inclusivos con sesgo, `resolve`
  mayormente *inlined*). Corrí una sola vez esa ventana.
- **Los 204 `throw`/s** son de una corrida bajo gdb (mismos ciclos, cuadros y PC final que sin gdb); no separé cuántos
  son `NonLocalReturn` y cuántos otros tipos, ni la profundidad de pila que recorren.
- **El costo por evento del scheduler y por ráfaga SQ en MIPS** (A5, A6) lo estimé contando el código fuente, no el
  desensamblado de esas funciones.
- **AICA en lotes y DSP apagado** (A6 b, A7): no verifiqué que el driver de sonido del ARM no lea registros que dependan
  de la mezcla o del DSP; solo que no hay interrupciones entre SH-4 y ARM y que el SH-4 casi no escribe registros AICA.
- **Video (5c) y idle-skip en PSP (5d)**: revisé diffs sin commitear en sus worktrees a la hora de esta revisión; pueden
  haber cambiado. No corrí su código.
- **`animal_forest_psp`** lo leí parcialmente antes de que se borrara; lo que cito de él (banderas del Makefile, código en
  el scratchpad) no lo pude volver a comprobar. De los otros tres ports que se borraron no uso nada.
- **`shar-psp-engine`**: cité su documentación de investigación del ME y dos pruebas; no verifiqué sus cifras ni que su
  código haga lo que su documentación dice.
- **Tamaño de A4 inline** (+2–3 MB) y de A12 (−0,5–1,5 MB) son estimaciones por sitio; no compilé ninguna variante.
- **TATE, costo de dibujo del GE y texturas** siguen sin ninguna medición.
