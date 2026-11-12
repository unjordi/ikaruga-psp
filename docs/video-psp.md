# Video en la PSP: recorte de usuario y niebla

Cómo dibuja `psp/stubs/video.cpp` el recorte de usuario del PVR2 y la niebla de tabla, y cómo se
prueba (`scripts/test-psp-video.sh`). Base: `docs/geometria-psp.md` (dibujo) y `docs/titulo-psp.md`
(texturas). Mediciones de la fila que lo construyó: `docs/mediciones/video-psp.md`.

Las pulsaciones son las del capítulo 1:
`start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200`.

## Recorte de usuario (`recorte=0` lo apaga)

Ikaruga dibuja el escenario con el recorte de usuario de la PCW (`tile_clip` 2, «solo dentro») con el
rectángulo del campo de juego, para que no se vea bajo las columnas del HUD. `clip_rects`
(`video.cpp`) escala el rectángulo `clip_x0..x1, clip_y0..y1` (píxeles de 640×480, derecha y abajo
exclusivos) a 480×272 redondeando al más cercano (un píxel cuenta si su centro está dentro, como en el
shader de PC), lo intersecta con la pantalla y lo manda a `sceGuScissor`. Modo 3 («solo fuera») dibuja
la tira hasta 4 veces, una por banda (arriba, abajo, izquierda, derecha), cada una con su scissor. El
scissor vuelve a pantalla completa en la siguiente tira sin recorte y al final del render.

## Niebla de tabla (`niebla=0` la apaga)

El GE no tiene niebla en modo through, así que `f` se calcula por vértice y la mezcla del shader de
PC `mix(color, niebla, f)` se parte en dos pasadas:

1. la tira normal con el color de vértice escalado por (1 − f) (decal pasa a modulate sobre un gris
   (1 − f) para poder escalar el texel);
2. la misma tira con `GU_TFX_BLEND` y color de vértice y de entorno f·niebla, sumada
   (`GU_FIX 1 + GU_FIX 1`; en translúcidas con src = alfa, `GU_SRC_ALPHA + 1`), profundidad `EQUAL`
   en opacas y sin escribir profundidad.

`f` sale de la tabla con la misma aritmética que `fog_factor` (`fog_table_factor`), o del alfa del
color offset para la niebla por vértice (modo 1 con offset). El modo 3 (tabla solo en alfa) sustituye
el color por el de la niebla con alfa `f`. Las translúcidas con otros factores de origen quedan sin
niebla. La segunda pasada cuesta ≈4,5 % del capítulo 1 (plan, paso «Abaratar la segunda pasada de niebla»).

En el log, la línea `fog:` del render capturado trae `density`, `col_ram` y la tabla; `fogged` cuenta
las tiras con niebla. No hay render a textura: el contador `texture_renders` (`FB_W_SOF1` bit 24, en
las líneas `render:` y `video:`) vale 0; si algún día sube, el EBOOT seguiría dibujando a pantalla.

## Prueba

```sh
PROBAR_DISCO=<DISCO-real.ISO> scripts/test-psp-video.sh build/psp/ikaruga/EBOOT.PBP $J [--reusar] [--solo a|b|c]
```

Cinco corridas de PPSSPP sin limitador, una a la vez, contra la serie de Flycast
(`docs/referencia-flycast.md`; ventanas en `scripts/escenas-flycast.sh`). Umbrales fijados antes de
medir:

| prueba | escena | umbral | contraprueba |
|---|---|---|---|
| (a) recorte | cuadro 2350, columnas laterales (izquierda x < 100, derecha x ≥ 380 a 480×272) | SSIM ≥ 0,7 en cada una | `recorte=0`: al menos una < 0,7 |
| (b) niebla | cuadro 1750 (SELECT GAME MODE, ventana 250-312) | pantalla completa ≥ 20 dB y SSIM ≥ 0,7; botones (60,105)-(270,225) ≥ 20 dB | `niebla=0`: botones < 20 dB |
| (c) título | cuadro 1243 | ≥ 25 dB y SSIM ≥ 0,85 (es la (a) de `test-psp-titulo.sh`) | — |

`scripts/bateria.sh` corre solo (a) y (b), porque (c) ya es la (a) del título.

## Qué no cubre

- Todo es PPSSPP. Las dos pasadas con `EQUAL` suponen que el GE da exactamente la misma profundidad a
  la misma tira dos veces; en PPSSPP se cumple, en una PSP real no está visto.
- Modo 3 del recorte y niebla de modos 1 y 3: no aparecen en los cuadros medidos (0 tiras en modo 3;
  solo niebla de tabla); el código está, ninguna prueba lo toca.
- La mezcla aditiva de la segunda pasada en translúcidas es una aproximación (exacta para
  `SRC_ALPHA / INV_SRC_ALPHA` y `ONE / ONE`).
- El centro del capítulo 1 en 2350 queda en 20,28 dB / SSIM 0,682 contra Flycast; la prueba (a) solo mira
  las columnas (`docs/referencia-flycast.md`; pasos de la sección «Se ve como el original» del plan).
- La región de los botones se fijó a ojo; no se barrió.
