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

## RAM libre

(pendiente)

## Qué NO verifiqué

(pendiente)
