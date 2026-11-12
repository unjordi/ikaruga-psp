# Texturas en la PSP y prueba del título

Cómo llegan las texturas del PVR2 al GE y cómo se prueba la pantalla de título
(`scripts/test-psp-titulo.sh`). Mediciones de la fila que lo construyó:
`docs/mediciones/titulo-psp.md`.

## Qué hay

- `psp/stubs/texture_psp.{h,cpp}`: caché de texturas PVR2 → GE. Por textura: `describe_texture` +
  `decode_texture` del submódulo sobre la VRAM y la paleta del guest (`unpack_palette` de
  `reg_block()+0x1000`, formato en `PAL_RAM_CTRL` 0x108), empaquetado al formato GE sin pérdida
  (1555→5551, 565→5650, 4444→4444; paletas según el formato de la paleta; YUV, bump y paleta 8888
  →8888), swizzle, lado máximo 512 (un lado de 1024 se reduce 2:1 con filtro de caja y las
  coordenadas se escalan con él). Las paletas se expanden en lugar de ir como CLUT del GE.
- Llave de la caché: `(tcw, tsp & 0x3F, espejo u, espejo v)`. En cada render se revalida cada
  textura usada: solo se vuelve a hashear si alguna de sus páginas de VRAM de 4 KB se escribió desde la
  última validación (`vram_write_epoch`, parche `0007`); la paleta se hashea siempre. Si cambió, se
  decodifica de nuevo (`invalidated`). LRU con tope `texturas_kb` (2560 KB por omisión; el
  presupuesto está en `docs/memoria-psp.md`); nunca se expulsa una imagen que el render en curso ya
  entregó, y `make_room` libera espacio antes de decodificar. Si el heap no alcanza para el RGBA8888
  temporal del decodificador, se libera lo que no esté en uso y se reintenta una vez; si aun así
  falla, el polígono se dibuja sin textura (no aborta).
- `psp/stubs/video.cpp`, estado por polígono desde la palabra TSP:
  - Shading → `sceGuTexFunc`: decal `GU_TFX_REPLACE`; modulate `GU_TFX_MODULATE` con alfa de
    vértice forzado a 1 (en el PVR el alfa sale solo del texel); decal alpha `GU_TFX_DECAL`;
    modulate alpha `GU_TFX_MODULATE`. `ignore_texture_alpha` → `GU_TCC_RGB`; `use_alpha=0` → alfa
    de vértice 1.
  - `filter_mode` 0 → `GU_NEAREST`, otro → `GU_LINEAR` (sin mipmaps, bilineal).
  - `clamp_u/v` → `GU_CLAMP`; además se fija clamp a todo polígono que no repite (`polygon_tiles`),
    como el backend Vulkan, para que el bilineal no traiga el borde opuesto.
  - Espejo (flip): el GE no lo tiene. La imagen guarda la textura y su espejo lado a lado y se
    muestrea con `GU_REPEAT`.
  - Mezcla: `SrcInstr/DstInstr` → `sceGuBlendFunc` (los ocho factores PVR existen en el GE, 0/1 como
    `GU_FIX`). `SrcSelect/DstSelect` (búfer secundario de acumulación) no existe en el GE y se ignora.
    Punch-through: alpha test `GU_GEQUAL PT_ALPHA_REF`. Orden opaca → punch-through → translúcida
    (atrás→adelante).
  - Niebla y recorte de usuario: `docs/video-psp.md`. No se dibujan el color offset (especular) ni el
    color clamp.
- `ikaruga.cfg`: `texturas=0` apaga las texturas; `texturas_kb=N` fija el tope de la caché.
- `scripts/comparar-titulo.sh` captura la PSP en el cuadro N y la compara con la serie de Flycast
  (`scripts/comparar-flycast.sh --captura`; `docs/referencia-flycast.md`).

## El cuadro del título

Sin entrada el juego nunca llega al título: se queda en la pantalla de arranque (negro con el logo
chico abajo a la derecha). Hace falta una pulsación: `start@700` (`pulsar=start@700` en la PSP; en
Flycast, Return a los 700/60 s de abrir la ventana).

| cuadros de la PSP | escena |
|---|---|
| 1210–1242 | entrada del título (fundido) |
| **1243** | primer cuadro idéntico al título quieto |
| 1260–1280, 1350, 1400 | parpadeo de «PRESS START BUTTON» (caja 175,334–462,345) |

La prueba mide el cuadro 1243 contra la ventana 170-205 de la serie de Flycast
(`scripts/escenas-flycast.sh`).

## Prueba

```sh
PROBAR_DISCO=<DISCO-real.ISO> scripts/test-psp-titulo.sh build/psp/ikaruga/EBOOT.PBP $J [--todas]
```

Corridas de PPSSPP sin limitador, una a la vez. Umbrales fijados antes de medir:

| prueba | qué compara | umbral |
|---|---|---|
| (a) | PSP 1243 con texturas contra la ventana del título | PSNR ≥ 25 dB y SSIM ≥ 0,85 |
| (b) | igual con `texturas=0` | PSNR < 25 dB (debe fallar) |
| (c) | la captura de (a) contra la ventana de SELECT GAME MODE | PSNR < 25 dB (debe discriminar) |
| (d) con `--todas` | las demás pruebas de la PSP (`arranque`, `rtc`, `carpeta`, `vmu`, `salida-suspension`, `igual-pc`, `geometria`, `redondeo`), cada una por separado | cada una pasa |

(d) pide lo que cada prueba pida; `test-psp-igual-pc.sh` necesita `DREAM_RECOMP`.

## Qué no cubre

- Todo es PPSSPP; el swizzle, `sceGuTexFlush` y el writeback de la caché de datos de las texturas
  pueden comportarse distinto en una PSP real.
- El parpadeo de PRESS START: el cuadro 1243 tiene el texto visible; no hay prueba con el texto apagado.
- Espejo, VQ, YUV, bump, paletas de 8 bits y paletas 8888 no aparecen en el título; el PSNR es global y
  una mezcla mal traducida en un área pequeña cabe dentro de él.
