# Geometría en la PSP

Cómo dibuja la PSP la geometría del PVR2 y cómo se prueba (`scripts/test-psp-geometria.sh`).
Mediciones de la fila que lo construyó: `docs/mediciones/geometria-psp.md`.

## Qué hay

- `psp/stubs/video.cpp` dibuja con `sceGu` el `dream::render::Frame` de cada render que arranca el
  guest (`pvr.on_render` en `psp/ikaruga/main.cpp`: `DisplayList::feed_stream` + `add_background`,
  como el lanzador de PC). Modo `GU_TRANSFORM_2D`, vértice
  `GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF`, escala 640×480 → 480×272 al convertir.
- Orden: opaca → punch-through (alpha test con `PT_ALPHA_REF`) → translúcida ordenada de atrás hacia
  adelante por tira, con la mezcla `sceGuBlendFunc` traducida de los campos SRC/DST del TSP y sin
  escribir z. Texturas, niebla y recorte: `docs/titulo-psp.md`, `docs/video-psp.md`.
- z-buffer de 16 bits: el 1/w de cada cuadro (`min_z..max_z`) repartido en 1..65535, comparación del
  ISP traducida tal cual (mayor = más cerca, borrado a 0). Doble búfer en eDRAM: 2 × 512×272×4 + z
  512×272×2 (1,33 MB de 2 MB). Sin orientación TATE (plan, paso «Pantalla en TATE en la PSP»).
- Lista del GE de 1 MiB: `video_draw_frame` corta el render y cuenta `dropped` antes de escribir más
  allá de `g_list`.
- Captura: copia del búfer visible a RAM con `sceGuCopyImage` (leer la eDRAM directo desde la CPU da una
  captura vacía en PPSSPP) y escritura RGB888 cruda en `captura-N.raw`.
- `ikaruga.cfg`: `capturar_en=N` (tras dibujar el primer render en o después del cuadro N escribe
  `captura-N.raw` y sale), `max_cuadros=N`, `pulsar=start@700,…` (sintaxis de `--press`; 8 cuadros cada
  una), `rtc_semilla=N`, `dibujar=0` (borra y presenta sin dibujar), `dibujar_desde=N` (no decodifica ni
  dibuja antes del cuadro N), `profundidad=0`, `trazar=1` (una línea por tira del render capturado),
  `volcar_ta=1` (escribe además `ta-N.bin`, la corriente del TA de ese render). Lista completa en
  `docs/primer-uso.md`.
- Con `rtc_semilla=1` la PSP no toma la hora real y el render de un cuadro dado es el mismo en cada
  corrida.
- Herramientas de depuración a mano, sin prueba: `scripts/contar-poligonos.cpp` (decodifica una
  corriente de `--dump-ta` del PC con el `DisplayList` del submódulo) y `tools/analizar-ta.cpp`.

## Prueba

```sh
PROBAR_DISCO=<DISCO-real.ISO> scripts/test-psp-geometria.sh build/psp/ikaruga/EBOOT.PBP $J [--cuadro N] [--reusar]
```

`comparar-geometria.sh` captura el cuadro N (2350 por omisión, inicio del capítulo 1) en PPSSPP sin
limitador, toma de la serie de Flycast la imagen de mayor SSIM dentro de `VENTANA_CAP1` (índices
380-420 de `scripts/escenas-flycast.sh`) y compara las máscaras de píxeles no negros (algún canal
> 24; el color de borrado es negro). Dos corridas en serie, ~35 s cada una:

- (a) cobertura de la PSP > 20 % de la pantalla e IoU ≥ 0,6 contra Flycast;
- (b) con `dibujar=0` la captura es de un solo color y la IoU queda bajo 0,6 (la prueba falla como debe;
  se pasa `--sin-borde` porque no se espera encontrar la escena).

Se usa el cuadro 2350 y no el 2600 porque Flycast corre por reloj de pared y la escena de la PSP en el
2600 no está en la serie en el mismo instante (`docs/referencia-flycast.md`).

## Qué no cubre

- La IoU es una medida gruesa: el campo de juego ocupa casi la misma área en los dos y la IoU sale alta
  aunque el interior difiera. Dice que forma y cobertura coinciden, no que el contenido sea igual.
- La mezcla de translúcidas con factores «other colour» (2/3) se tradujo por lectura del GE, sin
  prueba. La profundidad se comparó en un solo cuadro. Culling y volúmenes modificadores se ignoran.
- Todo es PPSSPP: ni el tiempo del GE ni el de la eDRAM se pueden sacar de aquí, y la presentación en
  pantalla no se probó (solo la captura).
