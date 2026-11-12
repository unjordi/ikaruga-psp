# Referencia: el EBOOT (PPSSPP) contra Flycast

La referencia de imagen es Flycast con el mismo disco. Las pruebas de imagen (título, video,
geometría, texturas del juego) comparan la captura de la PSP contra una serie grabada de Flycast;
ninguna lanza el PC recompilado (`ikaruga_boot` solo queda en `test-psp-igual-pc*.sh`, que compara
`write_hash` y no imágenes).

## Cómo se captura

**Flycast** (`scripts/capturar-flycast.sh <disco.chd>`): flatpak `org.flycast.Flycast//stable`
(v2.7). Lo que trae y lo que no:

- No trae Lua compilado: en el binario solo están las cadenas de la interfaz («Lua Scripting»,
  «Lua Filename»), ninguna de la API (`lua_pcall`, `luaL_*`). La línea de comandos solo acepta
  `-config sección:clave=valor` y la ruta del disco. No hay captura por número de cuadro.
- Lo que sí funciona: Flycast como ventana normal sobre Xvfb (`xvfb-run -d`, 640×480×24, OpenGL
  sobre llvmpipe; con `rend.ShowFPS` marca 59,8 fps, o sea tiempo real), las pulsaciones como
  teclas X con `xdotool` sobre la ventana «Flycast - Ikaruga» (mapeo de teclado por defecto:
  start = Return, a = x, b = c, flechas), y la pantalla grabada con `ffmpeg -f x11grab` a 10 fps.
- Una pulsación `boton@N` (sintaxis de `--press`/`pulsar=`) se manda N/60 s después de que aparece
  la ventana. Es reloj de pared, no cuadro exacto: por eso se alinea por escena (abajo).
- Config y datos de Flycast en `DIR/xdg/{config,data}` (`XDG_CONFIG_HOME`/`XDG_DATA_HOME`); la
  configuración global de la cachy no se toca. Cada corrida empieza sin archivo de VMU: el juego
  muestra «データファイルの作成に成功しました» y sigue. Sin BIOS: Flycast usa reios, región Japón.
- Cada cuadro grabado se escala a 480×272 con x·480/640, y·272/480, pantalla completa, filtro de
  caja (PIL `Image.BOX`).
- El disco se copia a `build/flycast/Ikaruga.chd` (el original puede ser de solo lectura; `FLYCAST_CHD`
  apunta a otro).

## La serie de referencia

`build/flycast/ref/` (ignorada por git): 60 s a 10 fps con las pulsaciones del capítulo 1
(`start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200`), 600 cuadros
`p480/f00001.png`…`f00600.png`. Se graba una vez y se reusa; si falta,
`comparar-flycast.sh --captura …` la graba sola (≈1 min, un solo Flycast):

```
scripts/capturar-flycast.sh build/flycast/Ikaruga.chd --segundos 60 --salida build/flycast/ref
```

Las ventanas de índices de cada escena están en `scripts/escenas-flycast.sh`, fijadas a ojo sobre
los mosaicos de la serie y con margen:

| escena | qué es | ventana (10 fps) | cuadro de la PSP |
|---|---|---|---|
| título | título quieto (logo, PRESS START) | 170-205 (plateau 176-200) | 1243 |
| SELECT GAME MODE | menú de modos, antes de que entre la pausa | 250-312 | 1750 |
| capítulo 1, inicio | nave frente a la torre | 380-420 | 2350 |

Las escenas del menú del título, la pausa y un punto avanzado del capítulo 1 no tienen ventana
fija: ninguna prueba las usa.

Dos grabaciones independientes de la serie dieron las tres escenas en los mismos índices (f178, f292,
f392) y las cifras de cada prueba se movieron ≤ 0,15 dB.

**Escena no encontrada.** `comparar-flycast.sh --captura` sale con 3 y lo dice cuando la ventana
cae fuera de la serie, o cuando la mejor imagen es un borde de la ventana y sigue mejorando hacia
fuera (SSIM del borde 0,02 por encima del de 3 cuadros adentro): la escena está cortada por la
ventana. Medido: ventana 590-700 (fuera) → exit 3; ventana 150-176 con la captura del título (la
escena empieza en 176) → exit 3, «f00176 ssim 0.867 … f00173: 0.418». Una ventana que queda después
de la escena (205-240) no se marca: da 10,79 dB y falla los umbrales. Las contrapruebas pasan
`--sin-borde` porque en ellas no se espera encontrar la escena.

## Cómo se compara

`scripts/comparar-flycast.sh --captura <captura-N.raw|png> --ventana F0-F1 [--region
NOMBRE:x0,y0,x1,y1]... [--exigir REGLA]... [--iou]`: toma de la ventana la imagen de **mayor SSIM
de pantalla completa** y da PSNR (RGB) y SSIM (luma BT.601, ventana uniforme 7×7, K1 0,01, K2 0,03)
de todo, izquierda (x 0-99), centro (100-379), derecha (380-479) y las regiones con nombre; con
`--iou`, la IoU de las máscaras de píxeles no negros (algún canal > 24). `--exigir
'REGIÓN:psnr>=V'` (o `ssim`, `iou`, `<`; `|` es «o») decide el código de salida. Ese criterio de
elección favorece a la PSP (elige su mejor momento dentro de la ventana). Modo de dos series
(`comparar-flycast.sh <serie-flycast/p480> <serie-psp> --escena NOMBRE:F0-F1:P0-P1`): lo mismo, pero
la PSP también es una serie (`PROBAR_GRABAR=DIR` en `probar-ikaruga-ppsspp.sh`).

## Umbrales y lo medido (build `float`)

Umbrales fijados antes de medir.

| prueba | escena (ventana → imagen) | umbral | medido | contraprueba |
|---|---|---|---|---|
| título (a) | 1243 (170-205 → f178) | todo ≥ 25 dB y SSIM ≥ 0,85 | 28,31 dB / 0,962 | `texturas=0`: 9,47 dB (< 25); contra SELECT GAME MODE: 9,94 dB (< 25) |
| video (b) | 1750 (250-312 → f292) | todo ≥ 20 dB y SSIM ≥ 0,7; botones ≥ 20 dB | 28,92 dB / 0,956; botones 27,96 dB | `niebla=0`: botones 15,81 dB (< 20) |
| video (a) | 2350 (380-420 → f392) | SSIM ≥ 0,7 en izquierda y derecha | 0,934 / 0,995 | `recorte=0`: 0,099 / 0,079 |
| geometría | 2350 (380-420 → f392) | IoU ≥ 0,6 | 0,9798 | `dibujar=0`: 0,0000 |
| texturas del juego (b) | 2350 (380-420 → f392) | todo ≥ 20 dB | 21,80 dB | (c) `texturas_kb=2048`: rechaza 10 907 pedidos |

**Por qué el cuadro 2350 y no el 2600.** Flycast va por reloj de pared y la PSP por cuadros: una
misma pulsación cae en instantes distintos del capítulo 1 y la escena que la PSP muestra en el 2600
(estructuras en diagonal) no está en la serie en ese instante. Barrido de la PSP contra la ventana
380-450 (PSNR / SSIM de pantalla completa): 2300 → 21,50 / 0,765; 2350 → 21,80 / 0,798; 2400 →
19,97 / 0,704; 2450 → 13,60 / 0,553; 2600 → 7,01 / 0,554 (mejor par de toda la serie). Los
umbrales no se tocaron; se eligió el punto del capítulo 1 que la serie contiene. El 2400 queda a
0,03 dB del umbral de 20 dB: la prueba de texturas del juego es sensible al cuadro elegido.

Las columnas laterales de la PSP son negras con el HUD, como en Flycast, en 2200, 2400 y 2600
(SSIM izquierda 0,93, derecha 0,995 en los tres). A ojo (capturas de 2200-2600) «CHAIN
STATUS» sale como una línea borrosa y la columna de cuadros del chain como una fila de puntos, en
vez de texto y cuadros huecos; no se midió por separado.

### Diferencia que queda

El centro del capítulo 1 en 2350: 20,28 dB / SSIM 0,682 (pantalla completa 21,80 / 0,798). Parte es
desfase entre las dos series y parte puede ser defecto de la PSP; no están separadas. Los pasos 38 a 45 del
plan lo atacan.

## Qué no cubre

- Las ventanas están fijadas a ojo; el mejor-SSIM dentro de ellas es objetivo, las ventanas no. El
  cuadro 2350 se eligió viendo el barrido de arriba (a la vista de los números, no antes).
- Flycast corre en tiempo real y las pulsaciones van por reloj de pared: no es cuadro exacto, y no
  se midió en cuántos cuadros de juego cae cada pulsación respecto a la PSP. Dos grabaciones dieron los
  mismos índices; no hay más de dos.
- La serie son 10 capturas por segundo: un instante corto puede caer entre dos.
- Flycast sin BIOS (reios) y región Japón; no se probó la rama `md5fix` ni un BIOS real. Un error de
  reios en la parte de juego no lo distinguiría de un error de la PSP.
- La IoU de geometría (0,98) es una medida gruesa: el campo ocupa casi la misma área en los dos.
- La detección de «escena no encontrada» solo cubre una ventana cortada por un borde o fuera de la
  serie; una ventana que contiene otra escena parecida pasa como cualquier comparación y la
  separan los umbrales.
- El archivo de VMU: Flycast arranca sin tarjeta y crea una; la PSP usa la suya en memoria. No
  se verificó que no cambie algo visible.
- Todo es PPSSPP contra Flycast: ninguna PSP real.
