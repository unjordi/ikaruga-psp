# Fila 9 — velocidad en la PSP (medido en PPSSPP)

*Números de PPSSPP 1.20.4 (flatpak), no de una PSP-3001. `speed` es guest_s / host_s de la línea
`capture written`; host_s es el reloj emulado de la PSP.*

## Cómo se mide

```
PROBAR_DISCO=.../f8/build/DISCO-real.ISO \
  scripts/medir-velocidad.sh <EBOOT.PBP> $D/games/ikaruga/extracted [--veces 2] [--salida DIR]
```

Dos corridas de cada caso: `juego` (capturar_en=2600 con las pulsaciones de
`test-psp-geometria.sh`) y `titulo` (capturar_en=1243, pulsar=start@700). Por corrida: `speed`,
`host_s`, `draw_ms_per_render` y la última línea `perf:` (contadores A9, promedio por cuadro de los
últimos 60 cuadros).

Criterio para conservar un cambio: ≥ 3 % en `juego` en las dos corridas, y las pruebas de
correctitud verdes.

## Línea base (develop adc3ddd)

EBOOT de develop sin cambios (sha256 `d9840d5c6ba3…`), `build/fila9-evidencia/velocidad-base/`:

| caso | corrida | speed | host_s | draw_ms_per_render | caché |
|---|---|---|---|---|---|
| juego 2600 | 1 | 0,0405 | 1070,989 | 109,81 | 152 imágenes, 2 029 952 B, over_budget 10 907, hash_ms 10 522,6, decode_ms 3 402,2 |
| juego 2600 | 2 | 0,0405 | 1070,989 | 109,81 | igual |
| título 1243 | 1 | 0,1453 | 142,928 | 0,86 | 8 imágenes, 868 352 B, 0 rechazos, hash_ms 864,0 |
| título 1243 | 2 | 0,1453 | 142,928 | 0,86 | igual |

- Las dos corridas dan el mismo número a la milésima: PPSSPP corre la PSP con reloj emulado, así
  que la medida es determinista y un cambio de 3 % se ve sin ruido.
- El 0,0405x del cuadro 2600 es más bajo que el 0,0524x de `geometria-psp.md`: aquel se midió con
  otro disco (`preparar-disco`, no el área HD real) y antes de que existieran las texturas. Con el
  disco real y texturas la meta sigue siendo 0,105x (no la muevo); desde 0,0405x eso es 2,6×.
- draw_ms_per_render en juego es el **promedio acumulado** desde el arranque (incluye renders que
  reconstruyen texturas); 109,81 ms frente a 0,86 ms del título.

## Cambios

### 1. A9 — contadores por cuadro (conservado, commit `cef6d65`)

Parche `patches/dream-recomp/0006-perf-counters.patch` (`dream::perf::counters`, compilado solo con
`-DDREAM_PERF_COUNTERS`) y la línea `perf:` del log cada 60 cuadros: FIPR/FTRV/FMAC por cuadro,
`throw` por cuadro y por segundo de host, callbacks del scheduler por cuadro, pedidos/aciertos/
construcciones/rechazos/rehash de la caché, ms de caché por cuadro y ms de dibujo por render.
El costo de los contadores no se midió aparte (son incrementos de un global); los números de abajo
ya los incluyen.

Lo que dicen (corrida `velocidad-dirty/juego-1`, host_s por cada 60 cuadros de guest):

| tramo | host_s / 60 cuadros | ftrv/f | fmac/f | fipr/f | rechazos/f | draw ms/render |
|---|---|---|---|---|---|---|
| título 300–1380 | 5,6–6,4 | 3 | 4–154 | 0 | 0 | 0,2–1,0 |
| juego 1500–1860 | 49–62 | 5 200 | 7 500 | 2 330 | 20–37 | 460–740 |
| juego 2040–2600 | 29–54 | 5 800–15 000 | 11 000–26 000 | 4 500–10 800 | 0 | 7–33 |

- throw: 3–8 por cuadro (≤ 30 por segundo de host): A13 no vale la pena aquí.
- scheduler: ~1 525 callbacks por cuadro, constante en título y juego: no explica el salto del
  juego; A6 queda al final.
- Orden decidido: **caché de texturas primero** (en 1500–1900 el dibujo pasa de 0,2 a 500–740 ms
  por render y coincide con los rechazos: cada pedido rechazado decodificaba la textura entera y
  solo entonces descubría que no cabía, y ese tiempo no entraba en `decode_ms`); luego **A3**
  (en 2040–2600 el dibujo ya es 7–33 ms y el resto, ~45 s de cada 60 cuadros, es CPU del guest,
  con 15 000 FTRV + 26 000 FMAC por cuadro en soft-double); A5/A6/A13 después.

### 2. Caché de texturas: páginas sucias + espacio antes de decodificar (conservado, commit `85658ea`)

- `patches/dream-recomp/0007-vram-dirty-pages.patch`: toda escritura que `DcMemory::resolve` manda a
  VRAM (store, write64, store queue, ruta de texturas del TA, DMA canal 2) estampa su página de 4 KB
  con `vram_write_epoch`; `main.cpp` pasa la tabla a `texcache_begin_frame` y sube la época tras
  cada render. Una imagen solo se vuelve a hashear si alguna de sus páginas se escribió desde que se
  validó (la paleta se sigue hasheando siempre: es chica).
- `build()` calcula el tamaño de la imagen GE y llama a `make_room` **antes** de decodificar: un
  pedido que no cabe ya no paga la decodificación.

| EBOOT | juego 2600 speed (2 corridas) | host_s | draw_ms_per_render | hash_ms |
|---|---|---|---|---|
| develop | 0,0405 / 0,0405 | 1070,989 | 109,81 | 10 522,6 |
| solo páginas sucias (`velocidad-dirty`) | 0,0409 / 0,0409 (+1,0 %) | 1062,365 | 105,48 | 410,1 |
| páginas sucias + espacio primero (`velocidad-cache2`) | **0,0517 / 0,0517 (+27,7 %)** | 839,883 | 8,67 | 413,3 |

Título con páginas sucias: 0,1453 → 0,1458 (+0,3 %), draw 0,86 → 0,25 ms. Comando:
`scripts/medir-velocidad.sh build/fila9-evidencia/DIR/EBOOT.PBP $G --salida build/fila9-evidencia/velocidad-DIR`.
Las páginas sucias solas no llegan al 3 %; se conservan con el segundo cambio porque son las que
quitan el hash de ~10 ms por render (hash_ms 10 522 → 410), y el par cumple el criterio.

Los rechazos **no** bajaron con esto: over_budget sigue en 10 907 (todos entre los cuadros 1500 y
1900; en el render capturado del 2600, rejected_this_render es 0). La demanda máxima de un solo
render (`peak_demand`) es 2 445 440 B, más que el tope de 2 MB. Ver el cambio 4.

### 3. A3 — FIPR/FTRV/FMAC en enteros exactos (revertido)

Se probó la opción (c) de la revisión sin tocar el resultado: `exact_dot.h` evalúa FIPR/FTRV como
la acumulación en `double` de la referencia (cada suma de double emulada con redondeo a 53 bits en
el modo de FPSCR.RM) y FMAC como `fmaf`, todo en enteros de 64 bits, y cae al código de referencia
en denormales, inf/NaN, cero o fuera de rango. Una prueba en PC con 8 M de entradas aleatorias por
operación, en los dos modos de redondeo, dio 0 diferencias de bits (no se usó la VFPU ni `float`,
así que la PSP seguiría escribiendo lo mismo que PC sin parche en el lado PC).

| EBOOT | juego 2600 speed (2 corridas) | host_s |
|---|---|---|
| caché (`velocidad-cache2`) | 0,0517 / 0,0517 | 839,883 |
| caché + A3 entero (`velocidad-exact2`) | 0,0516 / 0,0516 (−0,2 %) | 840,947 |

Sin ganancia: con ~25 % de las operaciones cayendo al camino de referencia (contador
`exact_declined`, ~2 900–6 200 por cuadro, sobre todo por ceros exactos en matrices/vectores) y el
resto pagando ~60 operaciones enteras de 64 bits (que en MIPS32 también son emuladas en pares de
registros), el costo queda igual al soft-double. Revertido; el parche y la prueba están fuera del
repo. Las opciones (a) VFPU y (b) `float` cambian el último bit frente a PC y exigirían el mismo
cambio en el binario de PC: no se hicieron en esta fila.

### 4. Presupuesto de la caché sin rechazos (conservado, commit `a2750eb`)

Una ventana de 2 MB no alcanza: el render más exigente del capítulo 1 pide 2 445 440 B
(`peak_demand`). Subir el tope a 2,5 MB sin más se queda sin heap en el cuadro 2491
(`fault: std::bad_alloc`, corrida `velocidad-exact3`). El runtime reservaba 2 MB de ROM de arranque
en ceros que con la BIOS HLE nunca se cargan ni se leen: `patches/dream-recomp/0008-optional-bios-image.patch`
(`-DDREAM_NO_BIOS_IMAGE`) no la reserva (lecturas de ROM dan 0, como el búfer vacío; escrituras se
descartan), y la caché pasa a 2 560 KB (`texturas_kb`, default nuevo).

| EBOOT | juego 2600 speed (2 corridas) | host_s | over_budget (toda la corrida) | decode_ms |
|---|---|---|---|---|
| caché 2 MB (`velocidad-cache2`) | 0,0517 / 0,0517 | 839,883 | 10 907 | 3 428 |
| caché 2,5 MB sin búfer de ROM (`velocidad-nobios`) | 0,0518 / 0,0518 (+0,2 %) | 837,556 | **0** | 1 315 |

No cumple el 3 % de velocidad, pero se conserva porque es lo que pide el punto 2 de la tarea: los
polígonos que salían sin textura ahora salen con textura (0 rechazos en toda la corrida, no solo en
el render capturado).

### 5. A5 — memo de UTLB y de rango MMIO en las store queues (revertido)

`sq_flush` probaba primero la entrada de la UTLB de la traducción anterior y `resolve` el último
rango MMIO acertado (la FIFO del TA), antes de las búsquedas lineales.

| EBOOT | juego 2600 speed | host_s |
|---|---|---|
| `velocidad-nobios` | 0,0518 | 837,556 |
| + memo (`velocidad-a5`) | 0,0528 (+1,9 %) | 821,314 |

Por debajo del 3 %: revertido (el reloj de PPSSPP es determinista, la segunda corrida da lo mismo).
El resto de A5 (`std::function` de `on_device_access`, `stream.insert` del TA) no se probó.

### Diagnóstico: cuánto pesa A3 de verdad (no conservado, no es un cambio)

Para saber si A3 basta para la meta, se construyó un EBOOT de diagnóstico con FIPR/FTRV/FMAC en
`float` simple (opción b, sin exactitud), sobre `velocidad-nobios`:

| EBOOT | juego 2600 speed | host_s |
|---|---|---|
| `velocidad-nobios` | 0,0518 | 837,556 |
| diagnóstico `float` (`velocidad-diagfloat`) | 0,0902 (+74 %) | 481,223 |

- Es la cota de lo que A3 puede dar en PPSSPP: ni así se llega a 0,105x.
- El `write_hash` del diagnóstico difiere del de develop desde el cuadro 1500 (primer cuadro de
  juego con FTRV): `float` cambia lo que el juego escribe, así que la opción (b) solo es aceptable
  si el binario de PC hace lo mismo (parche compartido) y se vuelve a validar contra Flycast. No lo
  hice: la tarea prohíbe relajar `test-psp-igual-pc.sh` y cambiar la referencia del PC es decisión
  del dueño.

### Fila 9b — FIPR/FTRV/FMAC en `float` en PC y en PSP (conservado)

Decisión del dueño: las tres operaciones pasan a `float` **igual en PC y en PSP**; el `double`
(y la FMA de FMAC) queda detrás de `-DDREAM_FPU_DOUBLE=1` para comparar. Ninguna de las dos es el
SH-4; lo que se conserva es que PC y PSP corran el mismo código.

- Parches: `patches/dream-recomp/0009-fpu-float.patch` (PSP, va tras 0006) y
  `patches/dream-recomp-pc/0002-sh4-FMAC-FIPR-and-FTRV-in-float-unless-DREAM_FPU_DOU.patch` (PC).
  El bloque `#if !DREAM_FPU_DOUBLE` de `ops.h` sale byte a byte igual en ambos árboles parchados
  (CONFIRMADO con `cmp`). Orden: FMAC `p = fr0*frm; p + frn`; FIPR `((a0*b0 + a1*b1) + a2*b2) + a3*b3`;
  FTRV igual por fila. Cada producto y suma redondeado a `float`.
- Sin FMA implícita en los dos lados: `-ffp-contract=off -frounding-math` (PC: `cmake/DreamTargetDefaults.cmake`,
  visto en `flags.make` de `dream_runtime` e `ikaruga_boot`; PSP: `CFLAGS` de `psp/ikaruga/Makefile`).
  Desensamblado: el `ikaruga_boot` float tiene 1 `cvtss2sd` y 0 llamadas a `fmaf` (el double: 1 068 y
  1 551); el ELF de PSP float tiene 0 `madd.s/msub.s` y 0 llamadas a `fmaf`.
- VFPU: no se usó. No hay prueba de que dé bit a bit lo mismo que el `float` escalar de PC, así que va
  FPU escalar.
- El `gen/` no cambia (el parche solo toca `runtime/`): sha256 medido `924f8b67…` con el parche.

| EBOOT | juego 2600 speed | host_s |
|---|---|---|
| `velocidad-nobios` (fila 9) | 0,0518 | 837,556 |
| **float (parche 0009)** | **0,0869** (+68 %) | 499,355 |
| float + vaciado de denormales por software (revertido) | 0,0834 | 520,436 |

Una corrida del final (la de 2 corridas del intento con vaciado dio dos números idénticos). Meta
0,085x: cumplida. Contadores al cuadro 2600: fipr/f 4 526, ftrv/f 5 927, fmac/f 10 893;
draw_ms/render 11,37. El resto del tiempo es CPU del guest fuera de estas tres operaciones.

**Igualdad PC↔PSP.** `test-psp-igual-pc.sh` acepta ahora `PULSAR=` (mismas pulsaciones en
PC `--press` y en PSP `pulsar=`). Sin entrada el juego no llega a escribir resultados de
FIPR/FTRV antes del cuadro 600: el PC float y el PC double dan el mismo `write_hash` en **todos**
los cuadros 0…20 000 sin pulsaciones, así que la prueba sin entrada no distingue float de double.
Con las pulsaciones de `medir-velocidad.sh` el PC float y el PC double difieren desde el cuadro 1480.

| prueba | resultado |
|---|---|
| (a) EBOOT float vs PC float, sin entrada, 60…600 | PASA (10 cuadros) |
| (a') igual, `PULSAR=<capítulo 1> HASTA=1980` | PASA 60…1980 (33 cuadros) |
| (a'') igual, `HASTA=2400` | FALLA en 2040; con `hash_every=1` el primer cuadro distinto es **1983** |
| (b) contraprueba: EBOOT float vs PC double, `PULSAR` `HASTA=1980` | FALLA en **1500** (PC `3558c903…`, PSP `125a570a…`; 24 iguales antes) |
| (b') EBOOT float vs PC double sin entrada | PASA: sin entrada la contraprueba no discrimina |
| control: EBOOT double (sin 0009) vs PC double, `PULSAR` `HASTA=2400` | FALLA también en 2040 |

Resto de la aceptación con el EBOOT float final (`build/eboot-noftz`, md5 `f2bb011b…`) y el binario
de PC float (`DREAM_RECOMP=build/dream-recomp`), 2026-11-10:

| prueba | resultado |
|---|---|
| (c) `test-psp-titulo.sh … --todas` | PASA: (a) 28,43 dB en el cuadro 1243; (b) sin texturas 9,46 dB; (c) contra el cuadro 800 de PC 7,37 dB |
| (e) las 8 de `--todas` | ok: arranque, redondeo, rtc, carpeta, vmu, salida-suspension, igual-pc, geometria (IoU 0,6055; su contraprueba sin dibujo da IoU 0 y falla como debe) |
| (d) `medir-velocidad.sh --solo 2600` | 0,0869x (meta 0,085x) |

La divergencia de 1983 **ya existía en double** (control). Es la escritura nº 164 579 168 en
`0x8C00EFA0` (fr1 guardado por `njGetTranslation` 0x0C014740, llamado desde 0x0C06D576): PC `0`,
PSP `0x057DE800`. Un PC compilado sin FTZ/DAZ (experimento, no conservado) iguala esa escritura y
difiere más adelante (nº 164 579 590); vaciar denormales por software solo en FIPR/FTRV/FMAC no la
cambió (mismo primer cuadro 1983) y costaba 4 %, por eso se revirtió. [INFER] el resto de la FPU
(fadd/fmul generados) en PPSSPP no hace el vaciado de denormales de FCSR.FS como x86 con FTZ/DAZ.
Queda abierto: es otra fila (correctitud), no la de float.

### 6. A6a — SPG con eventos solo en las líneas especiales (revertido por correctitud)

Evento del scheduler solo en las líneas 0, VBlank in/out, vstart, vbend y la de HBlank (modo 0);
la línea intermedia y `SPG_STATUS` se calculan del reloj.

| EBOOT | juego 2600 speed | host_s | write_hash vs `velocidad-nobios` |
|---|---|---|---|
| `velocidad-nobios` | 0,0518 | 837,556 | — |
| + SPG (`velocidad-a6`) | 0,0535 (+3,3 %) | 810,866 | **distinto desde el cuadro 240** (`d7e9cca4…` → `ee3fa477…`) |

Pasa el umbral de velocidad pero cambia lo que escribe el juego (mismos ciclos en el cuadro 240,
otro hash: probablemente el juego lee `SPG_STATUS` en un punto donde el valor calculado y el de un
evento por línea no coinciden, p. ej. el bit de VSync/blank entre eventos). Revertido sin correr
`test-psp-igual-pc.sh`: ya se sabe que fallaría. Los saltos de idle-skip bajaron de 2 791 805 a
1 879 494 al cuadro 2580 (cada evento de línea cortaba un salto); no medí `sched/f` en esa corrida.
Arreglarlo exige reproducir exactamente el `SPG_STATUS` por línea; queda pendiente.

### A13 — excepciones (no tocado)

Los contadores dan 3 `throw` por cuadro (4–13 por segundo de host) en el capítulo 1: aunque cada
uno costara 1 ms, sería < 1,5 % del tiempo. No se cambió nada.

## Resultado y pruebas de aceptación (EBOOT final = `nobios`, commit `a2750eb`)

| prueba | resultado |
|---|---|
| (a) `medir-velocidad.sh`, juego 2600 | **0,0518x / 0,0518x — no llega a 0,105x** (develop 0,0405x: +28 %) |
| (b) `test-psp-titulo.sh ... --todas` | PASS: título 28,43 dB; sin texturas 9,46 dB; contra cuadro 800 7,37 dB; las 8 pruebas (d) ok, `igual-pc` incluida |
| (c) `test-psp-texturas-juego.sh --psnr-ref 10.35` | PASS: 0 rechazos (over_budget + render); PSNR 10,35 dB = develop 10,35 dB; contraprueba `texturas_kb=2048`: 10 907 rechazos |

Qué limita la meta: con la caché arreglada el dibujo es ~7–11 ms por render y el resto es CPU del
guest. El diagnóstico `float` muestra que FIPR/FTRV/FMAC en soft-double son ~43 % del tiempo del
capítulo 1 (0,0518 → 0,0902), pero quitar el double cambia el `write_hash` frente a PC; la versión
exacta en enteros no gana nada. Lo siguiente con más palanca: A3 con la VFPU o `float` aplicado
**también al binario de PC** (decisión del dueño: cambia la referencia), y A6a con un
`SPG_STATUS` exacto (+3,3 % medido, hoy rompe la igualdad con PC). Aun sumando ambos (~0,093x) faltaría
algo más (A4/A10/A11 de la revisión).

Evidencia (no está en el repo; el sandbox no dejó escribir en `ajenos/ikaruga-tareas/fila9-evidencia`):
`build/fila9-evidencia/` del worktree: `velocidad-*/resumen.txt` y `psp.log` de cada corrida,
`texturas-develop.out`, `texturas-final.out`, `titulo-todas.out`.

## RAM libre

Línea `memory:` del log al capturar el cuadro 2600 (heap de newlib; `largest_block_kb` se mide
reservando bloques decrecientes; `kernel_free_kb` es lo que el kernel aún da fuera del heap):

| EBOOT | arena | en uso | libre en la arena | bloque mayor | kernel libre | caché |
|---|---|---|---|---|---|---|
| develop | 37 569 280 | 35 897 432 | 1 671 848 | 608 KB | 764 KB | 2 029 952 B (tope 2 MB) |
| caché 2 MB (`cache2`) | 37 764 352 | 35 897 688 | 1 866 664 | 528 KB | 764 KB | 2 029 952 B |
| final (`nobios`, tope 2,5 MB) | 36 175 360 | 34 389 208 | 1 786 152 | 2 016 KB | 764 KB | 2 617 728 B |

El búfer de ROM que ya no se reserva (2 MB) paga los 0,5 MB extra de caché y deja un bloque libre
de ~2 MB para la copia RGBA temporal de la decodificación (hasta 2 MB para 512×1024) y la captura.

## Qué NO verifiqué

- Todo es PPSSPP, nada en una PSP-3001 real: el reloj emulado no reproduce el costo real de
  caché/memoria; los porcentajes pueden ser distintos en hardware.
- La velocidad del título con el EBOOT final no la volví a medir (solo con páginas sucias:
  0,1458x); el cambio del búfer de ROM no debería tocarla, pero no está medido.
- El PSNR del cuadro 2600 es 10,35 dB en develop y en el final: es bajo porque la geometría ya
  difiere del PC (fila 5c); que no baje no prueba que las texturas antes rechazadas se vean bien,
  solo que la caché ya no las rechaza. No revisé la imagen a ojo.
- `DREAM_NO_BIOS_IMAGE`: supuse que nada lee la ROM de arranque con la BIOS HLE (solo
  `boot_main.cpp` del PC la llena); si el juego leyera el área 0 < 2 MB vería ceros, igual que antes.
- El costo propio de los contadores A9 no se midió por separado.
- El parche de páginas sucias marca solo escrituras que pasan por `DcMemory::resolve`; si algún
  camino escribiera VRAM con `vram()` directo, la caché no se enteraría (no encontré ninguno en el
  runtime parchado).

### Fila 9b ítem por ítem: qué cambia con `float` (EBOOT `build/eboot-noftz`)

Evidencia: `build/ev-vel-noftz/juego-1/psp.log` (juego 2600), `build/ev-vel-titulo/titulo-1/psp.log`
(título 1243), 2026-11-10/11. «No remedido» = EBOOT histórico de la fila 9, en `double`; su número
sigue valiendo para él y no lo reconstruí.

| ítem del doc | antes (fila 9, double) | con float | nota |
|---|---|---|---|
| Línea base juego 2600 (develop) | 0,0405 | no remedido | EBOOT histórico |
| Línea base título 1243 (develop) | 0,1453 | no remedido | EBOOT histórico |
| Título 1243 con el EBOOT final | sin medir (solo `dirty` 0,1458) | **0,1458**, host_s 142,375, draw 0,25 ms | cierra el «no medido» de la fila 9: el título casi no usa FTRV (3/f) |
| Tramo título 300–1380 (host_s/60 cuadros) | 5,6–6,4 | 6,3–6,4; ftrv 3, fmac 4–154, fipr 0 | sin cambio: casi sin FPU vectorial |
| Tramo juego 1500–1860 | 49–62; rechazos 20–37; draw 460–740 | **12,2–12,5**; ftrv 5 197, fmac ~7 500, fipr 2 334; rechazos 0; draw 6,7–7,7 | la caché (fila 9) quitó el dibujo; float baja el resto |
| Tramo juego 2040–2600 | 29–54 | **14,0–23,5**; ftrv 5 830–14 942, fmac 10 893–26 153, fipr 4 526–20 419; draw 7,6–33 | lo que queda es CPU del guest fuera de las tres ops + dibujo |
| throw por cuadro | 3–8 | 3–8 (≤ 39/s de host) | A13 sigue sin valer la pena |
| scheduler por cuadro | ~1 525 | 1 525–1 598 | igual |
| Cambio 2: develop / solo páginas sucias / páginas+espacio | 0,0405 / 0,0409 / 0,0517 | no remedido | el float va encima de los tres (conservado) |
| Cambio 3: A3 entero (`exact2`) | 0,0516, revertido | no aplica | lo sustituye el float |
| Cambio 4: `cache2` / `nobios` | 0,0517 / 0,0518, over_budget 10 907 / 0 | over_budget **0**, peak_demand 2 445 440, decode_ms 1 312,5, hash_ms 156,9 | la caché no cambia con float |
| Cambio 5: A5 memo (`a5`) | 0,0528 (+1,9 %), revertido | no remedido | sigue revertido; sobre float sería otro % |
| Diagnóstico `diagfloat` | 0,0902 (hash ≠ desde 1500) | float final 0,0869 | 4 % por debajo del diagnóstico: el `diagfloat` era otra variante (código no conservado), no lo reconstruí para comparar |
| Cambio 6: A6a SPG | 0,0535, hash ≠ desde 240, revertido | no aplica | sigue revertido por correctitud |
| A13 excepciones | no tocado | no tocado | ver fila throw |
| Resultado (a) velocidad juego 2600 | 0,0518 | **0,0869** | meta de 9b 0,085x cumplida; la de la fila 9 (0,105x) sigue sin cumplirse |
| Resultado (b) título `--todas` | 28,43 / 9,46 / 7,37 dB, 8 ok | 28,43 / 9,46 / 7,37 dB, 8 ok | idéntico |
| Resultado (c) texturas-juego `--psnr-ref 10.35` | PASA, 10,35 dB | **no corrido** | no lo pidió 9b; pendiente |
| RAM libre: develop / cache2 / final | in_use 35 897 432 / 35 897 688 / 34 389 208; bloque mayor 608 / 528 / 2 016 KB | final float: in_use 34 389 208, bloque mayor **2 000 KB**, kernel 764 KB | igual salvo 16 KB del bloque (medido por bloques decrecientes) |
| «Qué NO verifiqué» de la fila 9: hardware real | no | no | sigue |
| — velocidad del título con el EBOOT final | no medida | **medida: 0,1458** | cerrado |
| — PSNR 2600 bajo (10,35 dB) | no revisado a ojo | no revisado | sigue |
| — `DREAM_NO_BIOS_IMAGE` | supuesto | supuesto | sigue |
| — costo de los contadores A9 | no medido | no medido | sigue |
| — páginas sucias solo por `resolve` | no buscado más | no buscado más | sigue |

### Qué NO verifiqué en la fila 9b y dónde está mi punto ciego

- La igualdad PC↔PSP con `float` solo está probada **sin entrada (60…600) y con las pulsaciones del
  capítulo 1 hasta el cuadro 1980**. Desde el 1983 difieren, y también en `double`: la prueba por
  defecto (sin entrada) no ejercita FIPR/FTRV, así que es ciega a esta fila. No arreglé ni localicé
  del todo la divergencia de 1983 (pista: denormales fuera de las tres operaciones).
- La contraprueba (b) solo discrimina con `PULSAR`; sin entrada el EBOOT float pasa contra el PC
  double.
- `medir-velocidad` del EBOOT final: una sola corrida (las otras compilaciones daban corridas
  idénticas entre sí, pero esta no la repetí). Otros agentes corrían PPSSPP en la misma máquina en
  ratos; esperé a que no hubiera ninguno antes de lanzar cada prueba, pero no controlo la carga del
  host fuera de PPSSPP.
- VFPU no probada. Nada en hardware real: que el Allegrex real redondee como PPSSPP no lo sé.
- No revisé que `float` deje la imagen del capítulo 1 "bien" frente a Flycast; solo IoU de geometría
  contra el PC float.
- `build-psp-ikaruga.sh` reutiliza `obj/gen` aunque cambie `ops.h`: un primer intento midió un EBOOT
  sin el cambio. Rehíce con `obj/gen` borrado; no cambié el script (lo digo en primer-uso).


### Fila 8b ítem por ítem (EBOOT `float` con el parche 0010 apagado y la sonda `fpu:`, md5 `4b2d8f8b…`)

Medido el 2026-11-11 con `medir-velocidad.sh --veces 1` en PPSSPP (`build/f8b/vel/`). El parche 0010
solo actúa con `DREAM_SOFT_DAZ=1`; en `float` queda `Freg = float` y el código no cambia, salvo una línea
de sonda en el arranque.

| ítem del doc | antes (9b, `eboot-noftz`) | fila 8b | nota |
|---|---|---|---|
| Línea base juego 2600 (develop) | 0,0405 | no remedido | EBOOT histórico; 8b no toca develop |
| Línea base título 1243 (develop) | 0,1453 | no remedido | ídem |
| Juego 2600 (EBOOT final) | 0,0869, host_s 499,355 | **0,0869**, host_s 499,346, draw_ms_per_render 7,63 | igual: la sonda no cuesta |
| Título 1243 (EBOOT final) | 0,1458, host_s 142,375 | **0,1458**, host_s 142,369 | igual |
| Tramo título 300–1380 (host_s/60) | 6,3–6,4; ftrv 3, fmac 4–154, fipr 0 | 5,6–6,4; ftrv 3, fmac 2–154, fipr 0 | igual (la corrida de juego, mismos cuadros) |
| Tramo juego 1500–1860 | 12,2–12,5; ftrv 5 197 | 12,2–12,5; ftrv 5 196–5 198, fmac 7 504–7 715, fipr 2 334 | igual |
| Tramo juego 2040–2600 | 14,0–23,5 | 14,0–23,5 (ventana 1980–2040: 24,0); ftrv 5 830–14 942, fmac 10 893–26 153, fipr 4 526–20 419 | igual |
| throw por cuadro | 3–8 | 3–8 | igual |
| scheduler por cuadro | 1 525–1 598 | 1 525–1 598 | igual |
| Cambio 1 A9 (contadores) | conservado | sin cambio | lo usa esta tabla |
| Cambio 2: develop / solo páginas sucias / páginas+espacio | 0,0405 / 0,0409 / 0,0517 | no remedido | EBOOTs históricos; 8b no toca la caché |
| Cambio 3: A3 entero | revertido | no aplica | lo sustituye el float |
| Cambio 4: `cache2` / `nobios` | over_budget 10 907 / 0 | over_budget **0**, peak 2 621 056 de 2 621 440 | igual |
| Cambio 5: A5 memo | revertido | no aplica | sigue revertido |
| Diagnóstico `diagfloat` | 0,0902 | no aplica | diagnóstico histórico |
| Fila 9b: `nobios` / float / float+vaciado | 0,0518 / 0,0869 / 0,0834 | float 0,0869 | el vaciado revertido de 9b lo sustituye 0010, que solo existe en `double` |
| Igualdad (a) float sin entrada 60…600 | PASA | no corrida entera; arranque 60…240 **PASA** | regla de la fila: solo arranque a 240 |
| (a'), (a''), (b), (b') float vs PC con `PULSAR` | 1980 pasa / 2040 falla / 1500 falla / pasa | no aplica | en `float` una divergencia PC↔PSP por la FPU no es un bug; el PC no es referencia |
| Control: EBOOT double vs PC double, `PULSAR` | FALLA en 2040 (primer cuadro distinto 1983) | **PASA 60…2600** (EBOOT `f760c959…`) | era esta fila; contraprueba `DREAM_SOFT_DAZ=0` falla en 1983 (docs/correctitud-psp.md) |
| Resultado (c) `--todas` y (e) las 8 | PASA | no corrido | lo corre el orquestador al integrar |
| Resultado (d) / (a) velocidad juego 2600 | 0,0869 | 0,0869 | meta 9b 0,085x se mantiene; la de la fila 9 (0,105x) sigue sin cumplirse |
| Cambio 6: A6a SPG | revertido por correctitud | no aplica | sigue revertido |
| A13 excepciones | no tocado | no tocado | throw/f 3–8 |
| Resultado (c) texturas-juego `--psnr-ref 10.35` | no corrido en 9b | no corrido | 8b no toca el dibujo |
| RAM libre: final | in_use 34 389 208 | in_use **34 389 208**, arena 35 921 152, libre 1 531 944, bloque mayor 2 192 KB, kernel 764 KB | develop/cache2: EBOOTs históricos, no remedidos |
| Qué NO verifiqué de 9: hardware real | no | no | sigue |
| — PSNR 2600 bajo / `DREAM_NO_BIOS_IMAGE` / costo A9 / páginas por `resolve` | sigue | sigue | 8b no los toca |
| Qué NO verifiqué de 9b: divergencia de 1983 | abierta | **cerrada en `double`** | causa: la PSP no hace DAZ en las entradas, más el signo del NaN del double por software |
