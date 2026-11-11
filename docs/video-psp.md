# Video en la PSP: recorte de usuario (6b) y niebla (6c)

Continúa `docs/titulo-psp.md`. Todo medido en PPSSPP con el EBOOT de la variante mix
(`scripts/build-psp-ikaruga.sh <dr-idle>/build-rel/games/ikaruga/gen --variantes=mix`, sin
`DREAM_RECOMP`; imagen 14,81 MB + guest 28,1 MB = 42,91 MB de ~52 MB), disco
`PROBAR_DISCO=.../DISCO-real.ISO`, y contra Flycast con el mismo disco (`docs/referencia-flycast.md`).
Pulsaciones (las de `test-psp-geometria.sh`):
`start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200`.

## 6b: columnas laterales en el capítulo 1 (cuadro 2350)

**Causa.** Ikaruga dibuja el escenario con el recorte de usuario de la PCW (`tile_clip` 2, «solo
dentro»), con el rectángulo del campo de juego. La PSP lo ignoraba y el escenario se veía bajo las
columnas del HUD. En el cuadro 2600 hay 862 tiras con modo 2 y ninguna con modo 3.

**Arreglo** (`psp/stubs/video.cpp`, `clip_rects`): el rectángulo `clip_x0..x1, clip_y0..y1`
(píxeles de 640×480, derecha y abajo exclusivos) se escala a 480×272 redondeando al más cercano
(un píxel cuenta si su centro está dentro, como en el shader de PC), se intersecta con la pantalla
y va a `sceGuScissor`. Modo 3 («solo fuera») dibuja la tira hasta 4 veces, una por banda (arriba,
abajo, izquierda, derecha) con su scissor; no hace falta stencil. El scissor vuelve a pantalla
completa en la siguiente tira sin recorte y al final del render. `ikaruga.cfg recorte=0` lo apaga.

**Números** (cuadro 2350 contra la imagen de mayor SSIM de la ventana 380-420 de la serie de
Flycast, f392; izquierda = x < 100, derecha = x ≥ 380 a 480×272; umbral fijado antes de medir:
SSIM ≥ 0,7 en cada una):

| | izquierda | derecha | centro | pantalla completa |
|---|---|---|---|---|
| con recorte | **SSIM 0,934** (23,24 dB) | **SSIM 0,995** (31,48 dB) | 20,28 dB / 0,682 | 21,80 dB / 0,798 |
| `recorte=0` | **SSIM 0,099** | **SSIM 0,079** | — | — |

El centro sigue bajo: no es del recorte (ver punto ciego).

## 6c: botones de SELECT GAME MODE (cuadro 1750)

**Medido primero si hay render a textura**: no lo hay.
- PSP: contador de renders con `FB_W_SOF1` bit 24 (`texture_renders` en las líneas `render:` y
  `video:`): **0** hasta el cuadro 1700; en el render capturado `fb_w_sof1 0x0066a000`,
  `fb_w_ctrl 0x00000009`.

Así que no se implementó render a textura (el contador queda en el log para cuando aparezca).

**Tampoco era la caché**: en 1680–1700 `tex_req/f 769 tex_hit/f 769 tex_build/f 0 tex_rej/f 0`,
`over_budget 0`, `rejected_this_render 0`; ninguna textura rechazada ni mal decodificada.

**Causa real: niebla de tabla.** La PSP salía mucho más clara (media 136 frente a 88 de PC en toda
la pantalla; los botones, «blancos y negros», en 125/84 frente a 97/57; medido entonces contra el PC recompilado). Línea `fog:` nueva del
render capturado: `density 0x0000`, `col_ram 0x3c4632`, la tabla entera en `0x9999` (factor 0,6
para cualquier profundidad), y 696 de 739 tiras con niebla de tabla (TSP bits 22-23 = 0). Es un
velo gris verdoso del 60 % sobre la escena: 136·0,4 + ~60·0,6 ≈ 90, lo que da PC. El backend de
PC lo aplica en `geometry.frag` (`mix(colour, fog.table_colour, fog_factor(inv_w))`); la PSP no
dibujaba niebla.

**Arreglo** (`video.cpp`): el GE no tiene niebla en modo through, así que la mezcla se parte en dos
pasadas:
1. la tira normal con el color de vértice escalado por (1 − f) (decal pasa a modulate sobre un gris
   (1 − f) para poder escalar el texel);
2. la misma tira con `GU_TFX_BLEND` y color de vértice y de entorno f·niebla, sumada
   (`GU_FIX 1 + GU_FIX 1`; en translúcidas con src = alfa, `GU_SRC_ALPHA + 1`), profundidad `EQUAL`
   en opacas y sin escribir profundidad.
f por vértice: tabla con la misma aritmética que `fog_factor` (`fog_table_factor`), o el alfa del
color offset para la niebla por vértice (modo 1 con offset). Modo 3 (tabla solo en alfa) sustituye
el color por el de la niebla con alfa f. Translúcidas con otros factores de origen quedan sin
niebla. `ikaruga.cfg niebla=0` lo apaga.

| cuadro 1750, contra Flycast | botones (60,105)–(270,225) | pantalla completa |
|---|---|---|
| con niebla | **27,96 dB** | 28,92 dB / SSIM 0,956 |
| `niebla=0` | **15,81 dB** | — |

En el cuadro 2350 del capítulo 1 también hay niebla de tabla (`density 0xef0b`, `col_ram 0xe4cab4`, tabla decreciente
`ffe9 847a 463b 0904 0…`) y el centro se ve afectado.

**Costo**: `draw_ms/render` en 1680–1700 pasa de 6,78 a 13,38 ms (segunda pasada de 696 tiras);
en 2580–2600, 26,54 ms. En el título no hay niebla (`fogged 0`) y no cambia.

## Pruebas

`scripts/test-psp-video.sh <EBOOT> <extraído> [--reusar] [--solo a|b|c]` (cinco corridas de
PPSSPP sin limitador, una a la vez, ~3 min), contra la serie de Flycast (ventanas en
`scripts/escenas-flycast.sh`). Umbrales fijados antes de medir:

- (a) cuadro 2350: SSIM ≥ 0,7 en izquierda y en derecha; con `recorte=0`, al menos una < 0,7.
- (b) cuadro 1750 (SELECT GAME MODE, ventana 250-312): pantalla completa ≥ 20 dB y SSIM ≥ 0,7,
  botones (60,105)-(270,225) ≥ 20 dB; con `niebla=0`, botones < 20 dB.
- (c) el título (cuadro 1243): ≥ 25 dB y SSIM ≥ 0,85.

```
(a) ok: side columns ssim 0.934/0.995 >= 0.7; with recorte=0 0.099/0.079 (one < 0.7)
(b) ok: whole screen 28.92 dB / ssim 0.956, mode buttons 27.96 dB >= 20; with niebla=0 15.81 dB < 20
(c) ok: title 28.31 dB / ssim 0.962
```

Además, con el mismo EBOOT: `DREAM_RECOMP=$D scripts/test-psp-igual-pc.sh $E $G` y
`scripts/test-psp-arranque.sh $E $G` (resultado abajo). El video solo lee estado del guest (no hay
render a textura que escriba VRAM), así que el write_hash no debería moverse.

```
ok: PC and PSP write_hash equal at 10 frames (60..600)      # test-psp-igual-pc.sh, rc 0
ok: 5 distinct guest PCs over 7 log lines                   # test-psp-arranque.sh, rc 0
```

## Qué NO verifiqué y dónde está mi punto ciego

- **PSP-3001 real**: todo es PPSSPP. Las dos pasadas con `EQUAL` dependen de que el GE dé
  exactamente la misma profundidad a la misma tira dos veces; en PPSSPP se cumple, en hardware lo
  razoné pero no lo vi.
- **Render a textura**: no implementado porque no ocurre en 1700 ni antes (medido); si Ikaruga lo
  usa más adelante, el contador `texture_renders` lo dirá, pero la PSP lo seguirá dibujando a
  pantalla.
- **Modo 3 del recorte y niebla de modos 1 y 3**: no aparecen en los cuadros medidos (0 tiras en
  modo 3; en 1700 y 2600 sólo hay niebla de tabla). El código está, la prueba no lo toca.
- **Translúcidas con niebla y factor de origen distinto de 1 o alfa** quedan sin niebla; la mezcla
  aditiva de la segunda pasada en translúcidas es una aproximación (exacta sólo para `SRC_ALPHA /
  INV_SRC_ALPHA` y `ONE / ONE`).
- **El centro del 2350 sigue en 20,28 dB / SSIM 0,682 contra Flycast**: no averigüé por qué (candidatos: color offset/especular
  sin dibujar, volúmenes modificadores, filtrado). La prueba (a) sólo mira las columnas.
- **Región de los botones**: la fijé a ojo sobre la captura de la PSP; un desplazamiento de unos
  píxeles no cambia el veredicto (27 vs 16 dB), pero no lo barrí.
- **Costo**: la niebla duplica el tiempo de dibujo en escenas con niebla; no medí el efecto en la
  velocidad total del juego.

## El título tras 6b/6c (`scripts/test-psp-titulo.sh $E $G`, mismo EBOOT)

| prueba | valor | umbral |
|---|---|---|
| (a) PSP 1243 vs Flycast, con texturas | 28,31 dB / SSIM 0,962 | ≥ 25 dB y ≥ 0,85 |
| (b) igual con `texturas=0` | 9,47 dB | < 25 dB |
| (c) PSP 1243 vs SELECT GAME MODE de Flycast | 9,94 dB | < 25 dB |

Las 8 texturas del log `texture:` del cuadro 1243 son las mismas de la tabla de titulo-psp.md
(565 512×1024 → 5650 512×512; 2× 565 256×128; 2× 1555 512×32; 2× PAL4 256×128 y 1× PAL4 64×128
→ 4444). En el título `fogged 0`: no hay niebla, y el render sale idéntico al de antes.
