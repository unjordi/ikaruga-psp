# Referencia: el EBOOT (PPSSPP) contra Flycast

La referencia de imagen es Flycast con el mismo disco. Las pruebas de imagen (título, video,
texturas del juego) comparan la captura de la PSP contra la imagen de Flycast alineada con el mismo
cuadro (`scripts/flycast-alineado.sh`); la de geometría, contra la serie grabada por reloj;
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

## Flycast por cuadro exacto

`scripts/capturar-flycast-cuadro.sh <disco.chd> --cuadros N[,N...] [--pulsar P] [--retraso K]`
da la imagen de Flycast del cuadro N del guest con las pulsaciones en cuadros exactos (la serie por
reloj de pared de arriba sigue siendo la de las escenas quietas). Necesita `FLYCAST_BIOS`.

Cómo: el core de Flycast de RetroArch (flatpak `org.libretro.RetroArch`, instalación de usuario,
`flycast_libretro.so`). Se graba una vez una repetición (`retroarch -R`) sin entrada, desde el
arranque; `scripts/flycast-replay.py` reescribe en ella la máscara del pad (formato BSV v2 de
RetroArch 1.22: puerto 0, dispositivo 1, id 256) para que cada pulsación caiga en el cuadro pedido y
dure 8 cuadros como en la PSP; `retroarch -P … --max-frames=K+N --max-frames-ss` reproduce hasta ahí
y guarda la captura (640×480; `p480/` la baja a 480×272 como la serie). Cada corrida usa un
directorio propio con solo la BIOS: el core escribe VMU y `dc_nvmem.bin` junto a ella, y una VMU
sobrante cambia la primera pantalla («ロード» en vez de «作成»). La configuración de RetroArch y las
opciones del core (640×480, región Japón) son por corrida; la del usuario no se toca.

`K` (700) son cuadros del core antes del cuadro 0 del guest; basta que sea mayor que lo que tardan la
BIOS y el arranque del juego, porque el juego espera START en la pantalla del archivo de datos:
K = 560…720 da la misma imagen del cuadro 790 (43,14 dB contra la PSP en todos). Los cuadros de una
pulsación hacia delante son exactos; antes del primer START no hay cuadro de guest que medir.

Dos grabaciones nuevas (base regrabada, mismos argumentos) del 2350 y del 2700: idénticas byte a
byte. Con otro `K` (640): el 2700 idéntico y el 2350 a 51,08 dB (diferencia local en una explosión,
x 366-476, y 286-398).

`comparar-flycast.sh --captura <PSP.raw> --imagen build/flycast/cuadros/p480/f<N>.png` compara contra
esa imagen sin ventana ni búsqueda. Capítulo 1, PSP contra Flycast en el mismo cuadro: 2350 todo
17,22 dB / SSIM 0,669, `nubes:150,55,290,90` 19,46 / 0,676; 2700 todo 17,25 / 0,550, `nubes` 16,13 /
0,226 (iguales en las dos grabaciones).

**Los cuadros no coinciden en el tiempo.** El mejor cuadro de Flycast para la captura de la PSP no es
el mismo número: PSP 1000 ↔ Flycast 1003 (43,3 dB; el corte a negro cae en Flycast entre 1003 y
1004), PSP 1750 ↔ Flycast ≈1730, PSP 2350 ↔ Flycast ≈2334 (24,6 dB contra 17,2 en el mismo número).
Mover todas las pulsaciones ±2 cuadros no lo arregla. Causa medida en el paso 43 (abajo): la
numeración de cuadros del core no es la de los VBlank del juego; el capítulo 1 se compara en el
cuadro alineado por escena.

### Desfase de la línea de tiempo (paso 43)

Medido con `FLYCAST_ESTADOS=DIR` (nuevo en `capturar-flycast-cuadro.sh`: guarda el estado de
RetroArch de cada cuadro capturado) y `FLYCAST_OPCIONES="clave=valor ..."` (opciones del core por
corrida). La RAM de 16 MB se encuentra en el estado buscando 4 KB del código del juego (de
0x8C020000). Del lado del guest de la PSP se lee la misma RAM con el PC recompilado (`ikaruga_boot
--dump`), que es bit a bit el guest de la PSP en el build double (y el arranque igual hasta el 240 con
`test-psp-igual-pc.sh` en float); aquí el PC solo da contadores, no imágenes.

Contadores del juego encontrados comparando la RAM en los cuadros 1000 y 1001 del PC (palabras que
suben 1):
- `0x8C10BFC8`: VBlanks del juego. En el PC/PSP vale N−13 en todos los cuadros medidos (150…2350).
- `0x8C1523C0`: ticks de la lógica. En el PC/PSP vale N−75.

**1. Un cuadro del core es una imagen presentada, no un VBlank.** `retro_run` llama a
`emu.render()`, que corre el SH4 hasta que se presenta una imagen (`present()` →
`getSh4Executor()->Stop()`, `core/hw/pvr/Renderer_if.cpp:262-275`); sin render solo corta
`Emulator::vblank` tras 50 ms de SH4 (`core/emulator.cpp:1084-1095`), es decir ~3 VBlanks. Con las
pulsaciones de siempre (`K`=700), VBlank del juego (contador + 13, la numeración de la PSP) menos el
número de cuadro N de `capturar-flycast-cuadro.sh`:

| cuadro del core | 100-460 (antes de START) | 800, 1000 | 1200-1400 | 1500-1800 | 1900 | 2000-2700 |
|---|---|---|---|---|---|---|
| VBlank del juego − cuadro | +242 (1:1) | +243 | +245 | +282 | +293 | +304 |

Los saltos caen en las cargas de disco que siguen a las pulsaciones (1000→1200: +2; 1400→1500:
+37; 1800→1900: +11; 1900→2000: +11), donde el juego no renderiza. Por eso el desfase en imagen es
+3 en el 1000 y ≈−17…−20 desde el 1500: las pulsaciones del core caen en VBlanks del juego cada vez
más tarde y la escena llega antes en número de cuadro del core.

**2. Con las pulsaciones en el mismo VBlank del juego queda un retraso fijo de la lógica.**
Reponiendo cada pulsación en el cuadro del core cuyo contador de VBlank es el de la PSP
(`start@458,start@757,start@955,a@1155,start@1318,a@1518,start@1696,a@1896`, iterado leyendo
estados): en cada cuadro medido (700…2735) la lógica de Flycast va 6 ticks por detrás de la de la
PSP respecto al VBlank, y ya en el cuadro 100 del core (antes de cualquier pulsación). Es tiempo del
arranque: las lecturas del GD-ROM de Flycast con BIOS real frente al BIOS HLE del runtime (lectura
modelada con la tasa de Flycast, 5 sectores por millón de ciclos). Contraprueba:
`FLYCAST_OPCIONES=reicast_gdrom_fast_loading=enabled` lo cambia a −49 (la lógica adelanta), así que
ese número depende del tiempo de disco del emulador, y la lógica del juego cuenta ticks perdidos en
las cargas.

**3. Latencia de presentación de 3-5 cuadros.** Con las pulsaciones alineadas por ticks de la lógica
(`start@464,…,a@1902`), la mejor imagen de Flycast para la PSP del cuadro N tiene la lógica en
N+3 (1000: 37,49 dB), N+2 (1650), N+2 (2100), N+5 (2350: 25,40), N+5 (2700: 24,22). Con
`reicast_delay_frame_swapping=disabled` el fundido a negro del 1000 llega un cuadro del core antes
(negro en el 1003 en vez del 1004): parte de esa latencia es el intercambio diferido del core.
Ninguna alineación da la RAM igual (≈2500 de 98 304 palabras de 0x8C100000-0x8C160000 distintas en
la mejor): los 6 ticks cambian el estado del juego (azar, enemigos).

**Decisión: (a), de Flycast/RetroArch.** Los saltos son de la cuenta de cuadros del core y del tiempo
del disco emulado, que tampoco es el de una consola real; el guest de la PSP cuenta un VBlank por
cuadro como el PC. No hay nada que arreglar en el port. La prueba compara en el cuadro alineado por
escena: `scripts/escenas-flycast.sh` da `DESFASE_1000=3`, `DESFASE_1750=-20`, `DESFASE_2350=-17`,
`DESFASE_2700=-17` (pulsaciones de siempre, `K`=700), y `comparar-flycast.sh --captura … --imagen A
--imagen B …` toma la de mayor SSIM entre N+DESFASE−2 … N+DESFASE+2.

| PSP | mismo número: todo | alineado (Flycast) : todo | nubes:150,55,290,90 |
|---|---|---|---|
| 1000 | 29,04 dB / 0,997 | 1003: 43,35 / 0,999 | |
| 1750 | 29,44 / 0,894 | 1730: 31,34 / 0,930 | |
| 2350 | 17,22 / 0,669 | 2333: 26,73 / 0,948 | |
| 2700 | 17,25 / 0,550 | 2683: 26,04 / 0,904 | 30,58 / 0,924 (16,13 / 0,226 en el 2700) |

### Pasos 44 y 46 re-medidos en el cuadro alineado

Los datos de los pasos 44 (`ala:190,135,360,170`) y 46 (`cielo-sup:105,0,375,60`) eran contra la
serie por reloj de pared. Re-medidos (develop f7acfd8, build `float`, disco real, BIOS) con
`capturar-flycast-cuadro.sh --cuadros` N−21…N−13 y `comparar-flycast.sh --captura … --imagen …`
repetido; en los tres el mejor cuadro es N−17 (`DESFASE_2300=-17`, `DESFASE_2600=-17` nuevos):

| PSP ↔ Flycast | todo | ala | cielo-sup | nubes |
|---|---|---|---|---|
| 2300 ↔ 2283 | 30,12 / 0,964 | 24,24 / 0,844 | 31,43 / 0,946 | 27,48 / 0,878 |
| 2350 ↔ 2333 | 26,73 / 0,948 | 19,50 / 0,773 | 33,27 / 0,980 | 36,05 / 0,986 |
| 2600 ↔ 2583 | 22,74 / 0,856 | 19,53 / 0,615 | 22,47 / 0,842 | 20,71 / 0,788 |

A ojo (parejas ampliadas): en el 2300 el panel del satélite sigue saliendo en la PSP como un peine
de rayas oscuras donde Flycast dibuja un plano gris translúcido: **el defecto del paso 44 es real**.
El cielo alto del 2300 es igual en los dos (mismas nubes, misma línea del HUD): **las «franjas
diagonales» del paso 46 eran el desfase de la serie vieja**, no un defecto. Causa del peine: las
texturas mipmapped se subían sin cadena (docs/video-psp.md «Mipmaps»); con la cadena, `ala` 28,36 /
0,927 (2300), 20,10 / 0,797 (2350), 19,85 / 0,645 (2600), `cielo-sup` 32,81 / 0,956, 33,28 / 0,980,
23,75 / 0,874.

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

Umbrales fijados antes de medir. Título, video y texturas comparan contra Flycast alineado
(`comparar-titulo.sh --alineado`, la de mayor SSIM entre N+DESFASE−2 … N+DESFASE+2). Medido en develop
`d7dc10e`:

| prueba | escena (PSP ↔ Flycast) | umbral | medido | contraprueba |
|---|---|---|---|---|
| título (a) | 1243 ↔ 1249 (`start@700`) | todo ≥ 25 dB y SSIM ≥ 0,85 | 29,51 dB / 0,961 | `texturas=0`: 9,47 dB; contra SELECT GAME MODE alineado: 9,24 dB |
| video (b) | 1750 ↔ 1730 | todo ≥ 20 dB y SSIM ≥ 0,7; botones ≥ 20 dB | 31,34 dB / 0,930; botones 28,35 dB | `niebla=0`: botones 15,86 dB |
| video (a) | 2350 ↔ 2333 | SSIM ≥ 0,7 en izquierda y derecha | 0,984 / 0,998 | `recorte=0`: 0,116 / 0,085 |
| texturas del juego (b) | 2350 ↔ 2333 | todo ≥ 20 dB | 27,41 dB | (c) `texturas_kb=2048`: el contador sube (10 907 rechazos), la captura sale idéntica byte a byte |
| geometría | 2350, serie por reloj (380-420) | IoU ≥ 0,6 | 0,9798 | `dibujar=0`: 0,0000 |

La (c) de texturas no puede fallar por la imagen: los rechazos caen en otros cuadros, no en el
capturado. La IoU de geometría pasa casi por construcción (cualquier escena con geometría en el mismo
lugar).

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
- La serie de `capturar-flycast.sh` corre en tiempo real y las pulsaciones van por reloj de pared: no
  es cuadro exacto (para eso, `capturar-flycast-cuadro.sh`, arriba). Dos grabaciones dieron los
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
