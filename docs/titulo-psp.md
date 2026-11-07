# Pantalla de título con texturas en la PSP

## Qué hay

- `psp/stubs/texture_psp.{h,cpp}`: caché de texturas PVR2 → GE. Por textura: `describe_texture` +
  `decode_texture` del submódulo sobre la VRAM y la paleta del guest (`unpack_palette` de
  `reg_block()+0x1000`, formato en `PAL_RAM_CTRL` 0x108), empaquetado al formato GE sin pérdida
  (1555→5551, 565→5650, 4444→4444; paletas según el formato de la paleta; YUV, bump y paleta 8888
  →8888), swizzle, lado máximo 512 (un lado de 1024 se reduce 2:1 con filtro de caja y las
  coordenadas se escalan con él). Las paletas se expanden en lugar de ir como CLUT del GE: un solo
  camino y la invalidación por paleta queda acotada a las entradas que la textura lee.
- Llave de la caché: `(tcw, tsp & 0x3F, espejo u, espejo v)`. Cada render se vuelve a validar cada
  textura usada con un hash de sus bytes de VRAM y de las entradas de paleta que lee; si cambió se
  decodifica de nuevo (`invalidated`). LRU con tope `texturas_kb` (por defecto 2048 KB); nunca se
  expulsa una imagen que el render en curso ya entregó. Si el heap no alcanza para el RGBA8888
  temporal del decodificador, se libera lo que no esté en uso y se reintenta una vez; si aun así
  falla, el polígono se dibuja sin textura (no aborta).
- `psp/stubs/video.cpp`: estado por polígono desde la palabra TSP.
  - Shading → `sceGuTexFunc`: decal `GU_TFX_REPLACE`; modulate `GU_TFX_MODULATE` con alfa de
    vértice forzado a 1 (en el PVR el alfa sale sólo del texel); decal alpha `GU_TFX_DECAL`;
    modulate alpha `GU_TFX_MODULATE`. `ignore_texture_alpha` → `GU_TCC_RGB`; `use_alpha=0` → alfa
    de vértice 1.
  - `filter_mode` 0 → `GU_NEAREST`, otro → `GU_LINEAR` (trilineal sin mipmaps = bilineal).
  - `clamp_u/v` → `GU_CLAMP`; además se fija clamp a todo polígono que no repite
    (`polygon_tiles`), como hace el backend Vulkan, para que el bilineal no traiga el borde opuesto.
  - Flip (espejo): el GE no lo tiene. Se resuelve **en la textura**: la imagen guarda la textura y
    su espejo lado a lado y se muestrea con `GU_REPEAT`. En el título no aparece ninguna textura con
    espejo (todas `mirror 00` en el log), así que ese camino no está ejercitado.
  - Mezcla: `SrcInstr/DstInstr` → `sceGuBlendFunc`; los ocho factores PVR existen en el GE (0/1
    como `GU_FIX`). `SrcSelect/DstSelect` (búfer secundario de acumulación) no existe en el GE y se
    ignora. Punch-through: alpha test `GU_GEQUAL PT_ALPHA_REF`. Orden opaca → punch-through →
    translúcida (atrás→adelante), como en 5c.
  - No se dibuja: color offset (especular), niebla y color clamp.
- `ikaruga.cfg`: `texturas=0` apaga las texturas; `texturas_kb=N` fija el tope de la caché.
- `scripts/comparar-titulo.sh` (PSNR RGB y error medio por canal, PC escalada 640×480→480×272 con
  la misma transformación que los vértices: x·480/640, y·272/480, pantalla completa, filtro de
  caja) y `scripts/test-psp-titulo.sh` (pruebas a, b, c; `--todas` añade d).

## El cuadro N

Sin entrada, el PC **nunca llega al título**: de 300 a 4600 se queda en la pantalla de arranque
(negro con el logo chico abajo a la derecha). Hace falta una pulsación: `start@700`. Barrido en PC
(`ikaruga_boot --window --rtc-seed 1 --no-audio --press start@700 --screenshot-at n`, diferencia
contra el cuadro 1500):

| cuadros | escena |
|---|---|
| 730–900 | logo de TREASURE (otra escena; se usa 800 en la prueba c) |
| 950–1100 | fundido a blanco |
| 1210–1242 | entrada del título (diferencia media 80,8 → 1,5) |
| **1243** | primer cuadro idéntico (diferencia 0) al título quieto |
| 1260–1280, 1350, 1400 | parpadeo de «PRESS START BUTTON» (caja 175,334–462,345) |

N = 1243 con `pulsar=start@700` en ambos lados. Es el único desvío respecto al enunciado («sin
entrada»): sin esa pulsación no hay título que comparar.

## Resultados

Variables: `E=build/psp/ikaruga/EBOOT.PBP`, `D=~/code/ajenos/dr-idle`,
`G=$D/games/ikaruga/extracted`, `B=$D/build-rel/games/ikaruga/ikaruga_boot`,
`C=$D/games/ikaruga/ikaruga.toml`, `PROBAR_DISCO=.../f8/build/DISCO-real.ISO`.
Build: `scripts/build-psp-ikaruga.sh $D/build-rel/games/ikaruga/gen --variantes=mix` → imagen
14,81 MB + guest 28,1 MB = 42,91 MB de ~52 MB.

`scripts/test-psp-titulo.sh $E $G $B $C`:

| prueba | PSNR | error medio r/g/b | resultado |
|---|---|---|---|
| (a) PSP 1243 vs PC 1243, con texturas | **28,43 dB** | 3,86 / 3,04 / 2,69 | pasa (≥ 25) |
| (b) igual con `texturas=0` | 9,46 dB | 45,24 / 61,07 / 72,93 | falla como debe; 18,97 dB menos que (a) |
| (c) PSP 1243 vs PC 800 (logo) | 7,37 dB | 91,76 / 75,56 / 65,56 | < 25, discrimina |

(d) con el EBOOT final, cada una por separado (un PPSSPP a la vez):
`test-psp-{arranque,rtc,carpeta,vmu}.sh $E $G`, `test-psp-redondeo.sh`,
`DREAM_RECOMP=$D test-psp-{salida-suspension,igual-pc}.sh $E $G` (write_hash PC↔PSP igual en
60..600), `test-psp-geometria.sh $E $G $B $C` (IoU 0,6055, 947 polígonos en ambos): **las 8 pasan**.

Captura de la PSP (descrita, no subida): arte del título a color (la nave entre estelas
naranjas y amarillas), el kanji 斑鳩 con «IKARUGA» en su recuadro, el menú («PRESS START BUTTON»,
«DIFFICULTY/USER SELECT», «EXTEND/1ST 3,000,000», «EVERY 5,000,000», «PLAYER STOCK/3»), copyright y
URL legibles; columnas negras a los lados como en PC. Sin cuadros negros ni costuras visibles.

### Texturas del título (log `texture:` del cuadro 1243)

8 texturas, ninguna VQ, ninguna con mipmaps ni espejo:

| formato PVR2 | tamaño | disposición | n | → GE | bytes en caché |
|---|---|---|---|---|---|
| RGB565 | 512×1024 | lineal (stride) | 1 | 5650 512×512 (reducida) | 524 288 |
| RGB565 | 256×128 | lineal | 2 | 5650 | 2 × 65 536 |
| ARGB1555 | 512×32 | lineal | 2 | 5551 | 2 × 32 768 |
| PAL4 (paleta 4444) | 256×128 | twiddled | 2 | 4444 | 2 × 65 536 |
| PAL4 (paleta 4444) | 64×128 | twiddled | 1 | 4444 | 16 384 |

84 de las 110 tiras dibujadas llevan textura. Caché: 868 352 bytes, 8 decodificaciones, 0
invalidaciones, 0 expulsiones. La de 512×1024 es la única con pérdida (resolución vertical a la
mitad); en pantalla el título se ve a 272 líneas, así que la pérdida queda por debajo de lo visible.

### RAM libre al cargar el título (log `memory:`)

Con texturas: arena del heap 34 079 488, en uso 31 871 448, libre dentro de la arena 2 208 040
bytes, **bloque más grande obtenible 4016 KB**, memoria libre del kernel fuera del heap 764 KB.
Sin texturas: bloque más grande 6992 KB. (Una primera medición daba 65 520 KB porque el compilador
eliminaba el par malloc/free de la sonda; se corrigió con un puntero `volatile`.)

### Tiempo de cuadro en PPSSPP

`draw_ms_per_render` (media de `on_render` hasta el render 996, línea `video:`): **0,86 ms con
texturas, 0,19 ms sin**. Del extra, el hash de validación pesa más que la decodificación:
`hash_ms 864`, `decode_ms 352` acumulados en ~1040 renders. Reloj de PPSSPP hasta el cuadro 1243:
150 s con y 149 s sin (`psp-tiempo.txt`); velocidad 0,1455 vs 0,1462 del tiempo real. El costo de
las texturas en el título es despreciable frente a la emulación.

## Qué falló en el camino (y cómo quedó)

- Con tope de 4 MB, en juego (cuadro ~2491) el EBOOT abortaba con `std::bad_alloc`: el
  RGBA8888 temporal de una textura grande no cabía. Ahora el tope es 2 MB, se reintenta tras
  liberar, y la caché se vacía antes de la captura de pantalla (que pide ~1 MB).
- En el cuadro 2600 de juego la caché va al tope: 152 imágenes, 346 expulsiones y 10 907 pedidos
  rechazados por presupuesto (esos polígonos salen sin textura), y `draw_ms_per_render` sube a
  ~11 ms por el hash de validación. El título está bien; el juego no es de esta fila pero ese es el
  siguiente cuello.

## Qué NO verifiqué y dónde está mi punto ciego

- **PSP-3001 real**: todo es PPSSPP. El swizzle, `sceGuTexFlush` y el `Dcache` writeback de las
  texturas en main RAM pueden comportarse distinto en hardware; PPSSPP perdona más.
- **El parpadeo de PRESS START** no está cubierto: N cae en un cuadro con el texto visible; no
  comprobé un cuadro con el texto apagado.
- **Espejo (flip)**, VQ, YUV, bump, paletas 8 bits y paletas 8888: no aparecen en el título y no
  están ejercitados. La invalidación por reescritura de VRAM tampoco ocurrió (0 invalidaciones):
  el código está, la prueba no la toca.
- **Decal alpha y modulate con `ignore_texture_alpha`** traducen a combinaciones del GE que razoné
  pero no comparé polígono a polígono; el PSNR de 28 dB es global y una mezcla mal traducida en un
  área pequeña cabe dentro de él.
- **Color offset, niebla y color clamp** no se dibujan; en el título no parecen importar, en juego
  sí.
- **Presupuesto en juego**: con 2 MB hay polígonos sin textura en el cuadro 2600 y el hash por
  render cuesta ~10 ms. Hace falta validar por escrituras del guest (rango sucio) en lugar de hash,
  o usar CLUT del GE para las paletas.
- **Evidencia**: el sandbox no dejó escribir en `~/code/ajenos/ikaruga-tareas/fila6-evidencia/`;
  capturas y logs quedaron en `build/psp/evidencia/` y `build/fila6-evidencia/` del worktree, que el
  arnés borra. Los números de este documento son la única copia que queda.
