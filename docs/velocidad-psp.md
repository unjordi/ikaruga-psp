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

(pendiente)
