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
  muestra «データファイルの作成に成功しました» y sigue. Con `FLYCAST_BIOS=<dir con dc_boot.bin y dc_flash.bin>` los copia a `DIR/xdg/data/flycast` y Flycast arranca con esa BIOS (el log ya no dice «Did not load BIOS, using reios»); sin la variable usa reios, región Japón.
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
FLYCAST_BIOS=build/bios scripts/capturar-flycast.sh build/flycast/Ikaruga.chd --segundos 60 --salida build/flycast/ref
```

**La referencia es Flycast con la BIOS real del usuario** (`dc_boot.bin` y `dc_flash.bin` de su
Dreamcast, copiados a `build/bios/`, ignorada por git; nunca en el repositorio). Motivo: el juego
dibuja la leyenda del menú con la fuente de la boot ROM, y sin BIOS Flycast usa reios, cuya fuente no
es la de la consola; una referencia sin BIOS daría por buena una fuente que el juego real no tiene.
La BIOS real reproduce primero su animación de arranque (logo Dreamcast y SEGA, ≈ 9,7 s más en la
ventana de Flycast que con reios, medido: el logo de ESP aparece 97 cuadros más tarde a 10 fps), por
eso `capturar-flycast.sh` espera `FLYCAST_BIOS_RETRASO` s (9,7) antes de grabar y de mandar las
pulsaciones, y los índices de la serie y las ventanas de `escenas-flycast.sh` quedan iguales. El
EBOOT recibe la misma BIOS con `PROBAR_BIOS=<dc_boot.bin>`; `test-psp-igual-pc*.sh` corren sin ella
(el PC recompilado no tiene fuente).

Las ventanas de índices de cada escena están en `scripts/escenas-flycast.sh`, fijadas a ojo sobre
los mosaicos de la serie y con margen:

| escena | qué es | ventana (10 fps) | cuadros de la PSP |
|---|---|---|---|
| logo | logo de Treasure asentado | 126-156 | 750-900 |
| logo ESP | logo de ESP sobre gris | 158-168 | 1000 |
| título | título quieto (logo, PRESS START) | 170-205 (plateau 176-200) | 1050-1150 |
| menú | título con la lista GAME START | 201-232 | 1300-1400 |
| SELECT GAME MODE | menú de modos, antes de que entre la pausa | 250-312 | 1650-1850 |
| pausa | PAUSE MENU sobre la carga del capítulo 1 | 336-372 | 2050-2150 |
| capítulo 1, inicio | nave frente a la torre | 380-420 | 2300-2400 |
| capítulo 1, adelante | nave con escudo, torre y enemigos | 440-520 | 2700-3000 |

Cuadros de la PSP con las pulsaciones del capítulo 1 (`PRESSES_CAP1`). Con `start@700` solo, el
cuadro 1243 es el título; con las ocho pulsaciones ya es el menú.

Dos grabaciones independientes de la serie dieron las tres escenas en los mismos índices (f178, f292,
f392) y las cifras de cada prueba se movieron ≤ 0,15 dB. Una tercera grabación dio los mismos índices
y movió el 2350 de 21,80 dB / SSIM 0,798 a 21,13 / 0,743 (misma captura de la PSP, byte a byte): las
cifras del capítulo 1 se mueven con la grabación más que las de las escenas quietas.

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

## Escena por escena (develop, build `float`)

Cada escena: capturas de la PSP por cuadro exacto (`capturar_en=N`, `PROBAR_SIN_LIMITE=1`, las ocho
pulsaciones) contra la ventana de Flycast; de la ventana sale la imagen de mayor SSIM. Se mira la
pareja lado a lado y su diferencia (×4) y se anota cada diferencia visible. PSNR en RGB, SSIM en
luma, por región de 480×272.

| escena | cuadro PSP | imagen Flycast | todo | izquierda | centro | derecha |
|---|---|---|---|---|---|---|
| logo (Treasure) | 800 | f128 | 43,01 dB / 0,999 | ∞ / 1,000 | 40,67 / 0,998 | ∞ / 1,000 |
| logo ESP | 1000 | f167 | 37,44 / 0,999 | 38,59 / 1,000 | 36,78 / 0,998 | 38,59 / 1,000 |
| título | 1150 | f178 | 29,51 / 0,966 | ∞ / 1,000 | 27,17 / 0,942 | ∞ / 1,000 |
| menú | 1300 | f216 | 25,06 / 0,957 | 28,41 / 0,986 | 23,16 / 0,932 | 46,93 / 0,999 |
| SELECT GAME MODE | 1750 | f292 | 28,92 / 0,956 | 33,43 / 0,971 | 27,83 / 0,950 | 29,65 / 0,959 |
| pausa | 2050 | f342 | 23,64 / 0,943 | 22,30 / 0,926 | 22,74 / 0,928 | 68,71 / 1,000 |
| capítulo 1, inicio | 2350 | f392 | 21,13 / 0,743 | 23,24 / 0,934 | 19,48 / 0,589 | 31,48 / 0,995 |
| capítulo 1, adelante | 2700 | f450 | 19,49 / 0,706 | 23,24 / 0,934 | 17,55 / 0,525 | ∞ / 1,000 |
| capítulo 1, adelante | 3000 | f500 | 17,92 / 0,745 | 23,24 / 0,934 | 15,86 / 0,591 | ∞ / 1,000 |

Los demás cuadros medidos (todo, PSNR / SSIM): logo 750 44,36 / 0,999, 850 43,01 / 0,999, 900 43,21 /
0,999; título 1050 27,78 / 0,958, 1100 28,37 / 0,961; menú 1350 25,06 / 0,957, 1400 25,03 / 0,957;
SELECT 1650 25,68 / 0,903, 1700 29,91 / 0,956, 1800 28,80 / 0,950, 1850 27,83 / 0,944; pausa 2100
23,31 / 0,941, 2150 23,32 / 0,941; capítulo 1 inicio 2300 22,24 / 0,783, 2400 21,60 / 0,788;
adelante 2800 16,69 / 0,601, 2900 16,73 / 0,639. Se toma el cuadro de cada escena con el SSIM más
alto de los medidos, salvo el 2350, que es el de las pruebas de imagen (el 2400 llega a 0,788).
La región de las columnas laterales (izquierda, derecha) es el HUD; `∞` es una región idéntica.

### Diferencias visibles

Confianza alta: lo confirma el número de una región; media: solo se ve. «Defecto» es del port;
«instante» es desfase entre la PSP (por cuadro) y Flycast (por reloj de pared).

| # | escena | qué | dónde (480×272) | confianza | tipo | paso del plan |
|---|---|---|---|---|---|---|
| 1 | menú, pausa | la leyenda «ゲームを開始します。» / «ゲームを再開します。» no sale; la banda oscura sí | x 77-209, y 219-228; región `leyenda:70,214,220,234`: 10,59 dB / 0,329 (menú), 9,89 / 0,302 (pausa); sin leyenda 28,8-30,3 / 0,96-0,98 | alta | defecto | 39 |
| 2 | pausa, capítulo 1 | «CHAIN STATUS» borroso y las cajas del chain como puntos | x 20-75, y 40-251; `cajas:20,128,48,248` 14,86 dB / 0,608; `texto:20,40,80,55` 20,52 / 0,692 (2350) | alta | defecto | 40 |
| 3 | capítulo 1, adelante | el cielo sale como color plano naranja (2700) o cuña gris plana (3000), sin las nubes; borde de la cuña escalonado | x 105-260, y 0-135; `nubes:150,55,290,90` 17,95 dB / 0,564 (2700), 14,24 / 0,491 (3000) | alta (Flycast tiene nubes en todos sus cuadros 440-560) | defecto | 42 |
| 4 | capítulo 1, adelante | bordes escalonados en la torre y en la cuña del cielo | x 105-130, y 28-128 y x 295-320, y 0-130 (2700) | media | defecto | 42 |
| 5 | capítulo 1, inicio | franjas diagonales de luz en el cielo alto en vez de nubes | x 105-375, y 0-60; `cielo-sup:105,0,375,60` 21,31 dB / 0,687 (2300) | media | defecto | 45 |
| 6 | capítulo 1, inicio | el panel del satélite sale como rayas en peine, no como plano translúcido | x 197-350, y 140-165; `ala:190,135,360,170` 18,93 dB / 0,614 (2300) | media (la región no separa el desfase de posición) | defecto | 43 |
| 7 | pausa | falta «PRESS START» y el cursor ▶ en el 2050; en el 2100 salen | x 380-454, y 19-25 y el cursor a la izquierda de «Continue Game» | alta (par más cercano: 2100) | instante (parpadeo) | — |
| 8 | logo ESP | el gris del fondo difiere +2,8 niveles, uniforme | toda la imagen | media | instante (fundido) | — |
| 9 | menú | una fila de píxeles de la banda azul del cursor difiere en las columnas laterales | y 88, x 0-479 | media | defecto menor (1 px) | — |
| 10 | título, SELECT, capítulo 1 | contornos del texto «PRESS START» y del texto inferior del título con otro filtrado | x 380-454, y 19-25 (`press` 17,92-18,25 dB / 0,78-0,90) | media | defecto menor | — |
| 11 | logo, título, SELECT | sin diferencia visible más allá de contornos de 1 px | — | alta | — | — |

Las filas 9 y 10 no tienen paso: son de contorno (un píxel de desplazamiento o de filtrado) y no
cambian lo que se lee. Las filas 2 a 6 ya tenían paso (HUD, polígonos fuera de rango, mipmaps,
perspectiva); sus pasos llevan los datos nuevos; la fila 1 es el paso «Leyenda del menú del título y de la pausa».

### Contraprueba del método

La captura de la PSP de una escena contra la ventana de otra (`--sin-borde`) da números claramente
peores y la diferencia ×4 lo muestra entero:

| PSP | ventana | imagen elegida | todo |
|---|---|---|---|
| capítulo 1, inicio (2350) | título 170-205 | f176 | 15,48 dB / 0,593 |
| SELECT (1750) | capítulo 1 380-420 | f380 | 10,15 / 0,208 |
| título (1150) | SELECT 250-312 | f273 | 9,83 / 0,193 |
| pausa (2050) | adelante 440-520 | f494 | 10,11 / 0,462 |
| logo (800) | pausa 336-372 | f354 | 16,97 / 0,441 |

Contra la misma escena: 21,13-43,01 dB. El PSNR de pantalla completa separa mal los pares de
escenas casi negras (logo contra pausa: 16,97 dB); ahí manda el SSIM y mirar la imagen.

### Diferencia que queda

El centro del capítulo 1 en 2350: 20,28 dB / SSIM 0,682 (pantalla completa 21,80 / 0,798) en la
primera grabación; 19,48 / 0,589 (21,13 / 0,743) en la de la comparación escena por escena. Parte es
desfase entre las dos series y parte es defecto de la PSP: el cielo sin nubes y las rayas del
satélite (diferencias 3, 5 y 6 de arriba). Lo atacan los pasos de la sección «Se ve como el original» del plan.

## Qué no cubre

- Las ocho escenas se midieron con una sola grabación de la serie; las del capítulo 1 se mueven
  con la grabación (arriba) y sus diferencias de instante no se separaron por completo.
- Las ventanas están fijadas a ojo; el mejor-SSIM dentro de ellas es objetivo, las ventanas no. El
  cuadro 2350 se eligió viendo el barrido de arriba (a la vista de los números, no antes).
- Flycast corre en tiempo real y las pulsaciones van por reloj de pared: no es cuadro exacto, y no
  se midió en cuántos cuadros de juego cae cada pulsación respecto a la PSP. Dos grabaciones dieron los
  mismos índices; no hay más de dos.
- La serie son 10 capturas por segundo: un instante corto puede caer entre dos.
- Flycast con la BIOS real del usuario (una sola BIOS, la de su consola) y región Japón; no se probó
  la rama `md5fix`. El retraso de arranque de la BIOS (9,7 s) es una constante medida una vez.
- La IoU de geometría (0,98) es una medida gruesa: el campo ocupa casi la misma área en los dos.
- La detección de «escena no encontrada» solo cubre una ventana cortada por un borde o fuera de la
  serie; una ventana que contiene otra escena parecida pasa como cualquier comparación y la
  separan los umbrales.
- El archivo de VMU: Flycast arranca sin tarjeta y crea una; la PSP usa la suya en memoria. No
  se verificó que no cambie algo visible.
- Todo es PPSSPP contra Flycast: ninguna PSP real.
