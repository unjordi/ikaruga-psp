# Referencia: el EBOOT (PPSSPP) contra Flycast

Fila E. Compara lo que se ve en nuestro EBOOT en PPSSPP con Ikaruga corriendo en Flycast con el
mismo disco, escena por escena. El PC recompilado no aparece aquí: la referencia es el emulador.

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
- Cada cuadro grabado se escala a 480×272 con la misma transformación que `comparar-titulo.sh`
  usa con PC: pantalla completa, x·480/640, y·272/480, filtro de caja (PIL `Image.BOX`).
- El disco se copia a `build/flycast/Ikaruga.chd` (el original en /tank es de solo lectura).

**PSP**: `scripts/probar-ikaruga-ppsspp.sh` ganó `PROBAR_GRABAR=DIR` (y `PROBAR_GRABAR_FPS`,
default 1): graba la pantalla de PPSSPP (480×272) con `ffmpeg x11grab` durante toda la corrida.
Sin la variable no cambia nada. La corrida usada:

```
scripts/build-psp-ikaruga.sh /home/unjordi/code/ajenos/dr-idle/build-rel/games/ikaruga/gen --variantes=mix
printf 'max_cuadros=2700\npulsar=start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200\nrtc_seed=1\n' > build/psp-serie/ikaruga.cfg
PROBAR_DISCO=/home/unjordi/code/ikaruga-psp-wt/f8/build/DISCO-real.ISO PROBAR_CFG=build/psp-serie/ikaruga.cfg \
  PROBAR_GRABAR=build/psp-serie/rec PROBAR_GRABAR_FPS=1 \
  scripts/probar-ikaruga-ppsspp.sh build/psp/ikaruga/EBOOT.PBP <extraido> 2400 build/psp-serie/ikaruga.log
scripts/capturar-flycast.sh build/flycast/Ikaruga.chd --segundos 50 --salida build/flycast/s1
```

Las pulsaciones son las de `test-psp-geometria.sh` (incluyen `start@700` del título). La PSP llegó
al cuadro 2700 en 869 s de reloj (0,0518x en PPSSPP, con la grabación encendida): 1041 cuadros
grabados, ~2,6 cuadros de juego por captura.

## Cómo se alinea

Por escena, no por cuadro. Para cada escena se fija a ojo, mirando los mosaicos de las dos series,
una ventana de capturas que la encierra en cada lado; dentro de las dos ventanas
`comparar-flycast.sh` toma el par (Flycast i, PSP j) de **mayor SSIM de pantalla completa**. Ese
criterio favorece a la PSP (elige su mejor momento); aun así, si las dos series no contienen el
mismo instante (cap1-punto, abajo), parte del FAIL es desfase. Las ventanas usadas:

| escena | qué es | Flycast (10 fps) | PSP (1 fps) |
|---|---|---|---|
| titulo | título quieto (logo, PRESS START) | 176-200 | 125-140 |
| menu | menú del título (GAME START…) | 203-233 | 143-161 |
| select | SELECT GAME MODE | 251-300 | 176-200 |
| pausa | menú de pausa sobre el inicio del cap. 1 (lo abre `start@1600`) | 335-365 | 395-470 |
| cap1-inicio | inicio del capítulo 1 (texto sobre el cielo, nave en perspectiva) | 366-392 | 575-660 |
| cap1-punto | capítulo 1, «PLEASE WAIT», tramo de estructuras en diagonal | 420-445 | 795-830 |

## Números

`scripts/comparar-flycast.sh`: PSNR sobre RGB, SSIM sobre luma (BT.601, ventana uniforme 7×7,
K1 0,01, K2 0,03). Regiones de la pantalla 480×272: izquierda x 0-99, centro x 100-379,
derecha x 380-479. **Umbral fijado antes de medir: PSNR ≥ 20 dB y SSIM ≥ 0,7 en pantalla
completa** (para toda escena; la tarea lo fija para el título y no lo moví para las demás).

| escena | par elegido | todo | izquierda | centro | derecha | veredicto |
|---|---|---|---|---|---|---|
| titulo | fly 179 / psp 140 | 28,76 dB / 0,966 | iguales (negro) | 26,41 / 0,942 | iguales | PASS |
| menu | fly 204 / psp 149 | 25,07 / 0,957 | 28,39 / 0,986 | 23,18 / 0,933 | 45,88 / 0,999 | PASS |
| select | fly 251 / psp 177 | 28,09 / 0,893 | 29,86 / 0,864 | 27,38 / 0,879 | 28,89 / 0,963 | PASS |
| pausa | fly 339 / psp 428 | 21,96 / **0,577** | 20,32 / 0,173 | 22,33 / 0,856 | 23,07 / 0,174 | FAIL |
| cap1-inicio | fly 369 / psp 576 | 22,72 / **0,603** | 19,96 / 0,134 | 27,38 / 0,945 | 19,82 / 0,085 | FAIL |
| cap1-punto | fly 431 / psp 827 | **10,17 / 0,351** | 6,40 / 0,092 | 14,01 / 0,537 | 9,16 / 0,073 | FAIL |

Con una ventana más ancha para cap1-punto (Flycast 395-470, PSP 780-868) el mejor par es
fly 435 / psp 837: 10,67 dB / 0,376 (centro 17,13 / 0,577). Sigue fallando.

Luma media de las columnas laterales (0-255) en los pares elegidos — en Flycast son negras con
el HUD encima; en la PSP no:

| escena | Flycast izq / der | PSP izq / der |
|---|---|---|
| pausa | 3,6 / 1,5 | 13,8 / 13,7 |
| cap1-inicio | 3,2 / 0,1 | 21,4 / 20,4 |
| cap1-punto | 3,4 / 0,0 | 113,8 / 78,0 |
| titulo, menu, select | iguales en los dos (±3) | |

### Contraprueba

La PSP de una escena contra Flycast de otra (mismos pares elegidos):

| PSP | Flycast | PSNR | SSIM |
|---|---|---|---|
| titulo | select | 9,71 dB | 0,151 |
| select | titulo | 9,41 dB | 0,145 |
| cap1-punto | titulo | 9,11 dB | 0,123 |
| cap1-inicio | cap1-punto | 9,40 dB | 0,095 |

Todas muy por debajo del umbral: la métrica separa escenas, y las tres que pasan pasan porque
se parecen.

## Diferencias visibles (insumo de las siguientes filas)

Vistas en `build/flycast/comparacion/<escena>-{flycast,psp,dif}.png` y en los mosaicos; las de
«alta» confianza las confirman los números de la tabla de luma, las de «media» solo el ojo (y un
modelo de visión que a veces se equivoca).

1. **Columnas laterales con escenario en vez de negro** — cap1-inicio, pausa, cap1-punto; regiones
   izquierda y derecha. En Flycast el juego dibuja el campo en el centro
   y las columnas laterales quedan negras con el HUD encima. En la PSP el escenario 3D se ve en las
   columnas (en cap1-punto el cielo y las vigas llenan x 0-90 y x 385-480). Es la causa principal de
   los SSIM 0,07-0,17 de las laterales. Confianza alta. Sospecha (no verificada): falta o no tapa el
   polígono/recorte que oscurece las laterales (orden de dibujo, profundidad o recorte de la región
   de render).
2. **Franjas negras verticales estrechas dentro de la pantalla** — cap1-punto; en x ≈ 90-100
   (izquierda/centro) y x ≈ 375-385 (centro/derecha). En la PSP solo queda negra una banda angosta
   donde en Flycast empieza/termina el campo; parece el borde del panel negro que sí se dibuja, o el
   recorte de una capa. Confianza alta (luma 40 y 22 en esas bandas contra 0 en Flycast, y a ojo).
3. **«CHAIN STATUS» y la columna de cuadros del chain** — cap1-inicio, cap1-punto; región
   izquierda. En Flycast: el texto «CHAIN STATUS» bajo «1P» y una columna de cuadros huecos
   abajo. En la PSP, en cap1-punto, en lugar del texto hay un recuadro con una línea bajo «1P», y los
   cuadros se ven como una fila vertical de puntos. Confianza media.
4. **Texto del menú de pausa menos nítido** — pausa; centro. Mismo contenido y posición
   («PAUSE MENU», «Continue Game», la barra azul de selección), pero con bordes más gruesos o
   duplicados en la PSP. Centro 22,33 dB / 0,856: pasa por sí solo. Confianza media.
5. **Centro del cap. 1 en el tramo de juego** — cap1-punto; centro 14,01 dB / 0,537. Parte es
   desfase: la nave está en otra posición (las pulsaciones en Flycast van por reloj de pared y el
   escenario avanza a otra velocidad respecto a la serie de la PSP) y no hay el mismo instante en las
   dos series. Parte es la diferencia 1, que también ensucia el borde del centro. No separé las dos
   partes. Confianza baja sobre lo que sea defecto de la PSP.
6. **«PRESS START» arriba a la derecha** — cap1-inicio; región derecha/centro superior. El modelo de
   visión lo ve en la PSP y no en Flycast; puede ser solo el parpadeo del texto en otro instante.
   No confirmado.

Lo que **sí** se ve como en el emulador: título (logo, PRESS START, laterales negras), menú del
título y SELECT GAME MODE, todo por encima de 25 dB y 0,89 de SSIM; el texto del inicio del cap. 1
y el cielo con la estructura en diagonal (centro 27,38 dB / 0,945).

## Qué NO verifiqué y dónde está mi punto ciego

- Las ventanas de cada escena las puse yo a ojo; el mejor-SSIM dentro de ellas es objetivo, las
  ventanas no. En cap1-punto no hay garantía de que las dos series contengan el mismo instante.
- Flycast corre en tiempo real y las pulsaciones van por reloj de pared: no es cuadro exacto, y
  no medí cuántos cuadros de juego cae cada pulsación respecto a la PSP.
- La PSP se grabó a 1 captura por segundo de reloj (~2,6 cuadros de juego): un instante corto
  puede caer entre dos capturas.
- Flycast sin BIOS (reios) y región Japón; no probé la rama `md5fix` ni un BIOS real. Un error de
  reios en la parte de juego no lo distinguiría de un error de la PSP.
- No miré el código de render para confirmar la causa de las diferencias 1-3: son sospechas.
- El archivo de VMU: Flycast arranca sin tarjeta y crea una; la PSP usa la suya en memoria. No
  verifiqué que no cambie algo visible (no lo vi).
- Corrí solo una serie por lado: no medí cuánto varían los números entre corridas.
- Capturas y series viven en `build/` del worktree (el arnés lo borra): los números de este
  documento son la única copia.
