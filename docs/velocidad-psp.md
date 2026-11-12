# Velocidad en la PSP

Cómo se mide la velocidad del EBOOT, qué hace cada optimización que está en el código y en qué
punto está. Las tablas de cada medición intermedia están en `docs/mediciones/velocidad-psp.md`.

Todo es PPSSPP 1.20.4 (flatpak), no una PSP-3001. `speed = guest_s / host_s` de la línea
`capture written` del log; `host_s` es el reloj emulado de la PSP, así que la medida es determinista
(dos corridas dan el mismo número) y un cambio de 3 % se ve sin ruido. La imagen se juzga contra
Flycast (`docs/referencia-flycast.md`); aquí solo importa cuánto corre.

## Cómo se mide

```sh
PROBAR_DISCO=<DISCO-real.ISO> scripts/medir-velocidad.sh build/psp/ikaruga/EBOOT.PBP $J [--veces N] [--solo 2600|1243] [--salida DIR]
```

Dos casos, con el limitador de 60 Hz de PPSSPP puesto (`PROBAR_SIN_LIMITE=0` forzado): `juego`
(`capturar_en=2600` con las pulsaciones del capítulo 1) y `titulo` (`capturar_en=1243`,
`pulsar=start@700`). Por corrida imprime `speed`, `host_s`, `draw_ms_per_render` (promedio acumulado
desde el arranque) y la última línea `perf:` del log (contadores por cuadro promediados sobre los
últimos 60 cuadros). Sale con 1 si una corrida no dio velocidad. Un cambio de velocidad se conserva si
gana ≥ 3 % en `juego` y las pruebas de correctitud siguen verdes.

Un solo PPSSPP a la vez: otra instancia en la misma máquina no cambia el reloj emulado, pero sí el
tiempo de pared.

## Dónde está hoy

| caso | speed | host_s |
|---|---|---|
| capítulo 1, cuadro 2600 (`float`, `mix`) | 0,0861x (≈5 fps) | 503,9 |
| título, cuadro 1243 | 0,1458x | 142,4 |

La meta de jugabilidad es ≈30 fps; faltan los pasos de la sección «Velocidad» del plan. En el
capítulo 1 el dibujo cuesta ≈7–26 ms por render y el resto es CPU del guest. Contadores por cuadro
en el 2600: `fipr/f 4526`, `ftrv/f 5927`, `fmac/f 10893`, `sched/f 1598`, `tex_hit/f 985`; `throw`
3–8 por cuadro (≤ 1,5 % del tiempo; no vale optimizar excepciones, plan A13).

## Lo que está en el código

- **Contadores de rendimiento** (`patches/dream-recomp/0006-perf-counters.patch`,
  `-DDREAM_PERF_COUNTERS`): FIPR/FTRV/FMAC, `throw`, callbacks del scheduler, pedidos/aciertos/
  construcciones/rechazos de la caché de texturas y ms de caché y de dibujo, en la línea `perf:` cada
  60 cuadros.
- **Caché de texturas** (`texture_psp.cpp`, parche `0007-vram-dirty-pages.patch`): una imagen solo se
  vuelve a hashear si alguna de sus páginas de VRAM se escribió; `make_room` libera espacio antes de
  decodificar, de modo que un pedido que no cabe no paga la decodificación. Tope `texturas_kb` = 2560.
  El render más exigente del capítulo 1 pide 2 445 440 B (`peak_demand`); con un tope de 2 MB se
  rechazan 10 907 pedidos (esos polígonos salen sin textura), con 2560 KB ninguno
  (`scripts/test-psp-texturas-juego.sh` lo comprueba).
- **Sin imagen de Boot ROM** (`0008-optional-bios-image.patch`, `-DDREAM_NO_BIOS_IMAGE`): la BIOS HLE
  no la lee; ahorra 2 MB de heap, que pagan el tope de 2560 KB de la caché.
- **FIPR/FTRV/FMAC en `float`** (`0009-fpu-float.patch` en la PSP y
  `patches/dream-recomp-pc/0002-…` en el PC): FMAC `p = fr0*frm; p + frn`; FIPR
  `((a0*b0 + a1*b1) + a2*b2) + a3*b3`; FTRV igual por fila, cada producto y suma redondeado a
  `float`. Sin FMA implícita (`-ffp-contract=off -frounding-math` en los dos lados). `-DDREAM_FPU_DOUBLE=1`
  deja el `double` para comparar bit a bit con el PC (`docs/correctitud-psp.md`). El `gen/` no
  cambia (solo se toca `runtime/`), pero `ops.h` va en línea en el C++ generado: tras cambiar un parche
  que lo toque, `scripts/build-psp-ikaruga.sh --limpio`. La VFPU no se usa. Es la mayor ganancia
  medida: 0,0518x → 0,0869x en el capítulo 1.
- **Idle-skip barato** (`0012-idle-skip-cheap.patch`; `docs/idle-skip.md`).
- **Camino rápido a la RAM** (`0013-fast-ram-path.patch`): `Memory::read8…64/write8…64` dejan de ser
  virtuales; el área 3 (`(a & 0x1C000000) == 0x0C000000`, `a < 0xE0000000`) va directo a
  `fast_ram_ + (a & 0x00FFFFFF)` y lo demás cae a los virtuales `*_slow`. Las escrituras a RAM siguen
  llamando `note_ram_write` (`write_hash`, ventana de log, watch). Gana ≈1,1 % de `host_s` y 0,77 MB de
  imagen (14,13 MB): el tiempo no está en el despacho de memoria. Forzar `always_inline` en los ocho
  accesos sube la imagen a 22,06 MB y el juego muere con `std::bad_alloc` en el cuadro 1482. Lo que
  sigue costando por escritura es `note_ram_write`, porque el lanzador arma siempre `hash_writes`
  (plan, paso «El build de entrega deja de pagar lo que no necesita»).
- **Variante `mix`**: runtime, stubs y `main.cpp` a `-O2`, el generado a `-Os` (la variante todo `-O2`
  no cabe en 64 MB). Frente a todo `-Os`, ≈1,3x en el arranque.

## Lo que se probó y no está

- FIPR/FTRV/FMAC en enteros exactos de 64 bits: 0,0516x contra 0,0517x; ~25 % de las operaciones caen
  al camino de referencia y el resto paga ≈60 operaciones enteras en MIPS32.
- Memoria de la UTLB y del último rango MMIO en las store queues (plan A5): +1,9 %, bajo el criterio;
  el resto de A5 (`std::function` de `on_device_access`, `stream.insert` del TA) no se probó.
- SPG con eventos solo en las líneas especiales (plan A6a): +3,3 %, pero cambia lo que el juego escribe
  desde el cuadro 240 (probablemente lee `SPG_STATUS` entre eventos); exige reproducir `SPG_STATUS` por
  línea (plan, paso «SPG con menos eventos»).
- Vaciar denormales por software solo en FIPR/FTRV/FMAC: −4 % y no cambia la igualdad con el PC (la
  entrada denormal entra por los `fadd`/`fmul` generados).

## Qué no cubre

- Hardware real: el reloj emulado no reproduce el costo real de la caché ni de la memoria de la PSP.
- El costo propio de los contadores `perf` no se midió aparte (incrementos de un global).
- `DREAM_NO_BIOS_IMAGE` supone que nada lee la ROM de arranque con la BIOS HLE.
- El PSNR del capítulo 1 contra Flycast se mide en `docs/referencia-flycast.md`; que la caché no
  rechace no prueba por sí solo que cada textura se vea bien.
