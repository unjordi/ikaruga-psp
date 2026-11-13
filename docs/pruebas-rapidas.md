# Pruebas rápidas en PPSSPP

Cómo se hace que las pruebas de imagen y de correctitud tarden poco en reloj de pared sin cambiar lo
que miden, y cómo se corren. Las tablas de tiempos de la fila que lo construyó están en
`docs/mediciones/pruebas-rapidas.md`.

`wall_s` es el reloj de pared del host; `host_s` es el reloj emulado de la PSP
(`sceKernelGetSystemTimeWide`). Todo en PPSSPP 1.20.4 (flatpak).

## PPSSPP sin limitador: `PROBAR_SIN_LIMITE=1`

PPSSPP 1.20.4 no tiene opción de línea de órdenes ni clave de `ppsspp.ini` que arranque sin límite de
cuadros: lo único que lo quita es la tecla de avance rápido (`VIRTKEY_FASTFORWARD`, `Tab` en el mapa por
omisión; `Core/HLE/sceDisplay.cpp` `FrameTimingLimit()` devuelve 0 con `fastForward`). Por eso
`scripts/probar-ikaruga-ppsspp.sh` con `PROBAR_SIN_LIMITE=1` mantiene `Tab` abajo con
`xdotool keydown Tab` dentro del Xvfb, una vez por segundo (una pulsación antes de que exista la pantalla
del juego se pierde; repetirla no hace daño). Necesita `xdotool`.

El tiempo emulado no cambia (mismos ciclos, `host_s`, capturas y `write_hash`); solo deja de esperar al
vsync del host. Por eso no sirve para medir velocidad: `scripts/medir-velocidad.sh` lo fuerza a 0.
Llegar al cuadro 2600 pasa de ≈531 s a ≈36 s de pared. Si el foco no llega a la ventana de PPSSPP la
prueba corre con limitador: sale bien pero lenta, y no hay una comprobación automática de que el
limitador se apagó (solo el `wall_s`).

Usan `PROBAR_SIN_LIMITE=1` por omisión `comparar-titulo.sh`, `comparar-geometria.sh` (y con ellos
`test-psp-titulo`, `-video`, `-texturas-juego`, `-geometria`) y `test-psp-igual-pc.sh`.
`PROBAR_GRABAR` (la serie para Flycast) lo apaga, porque la grabación muestrea el tiempo de pared.

## `dibujar_desde=N` en `ikaruga.cfg`

Antes del cuadro de guest `N` el render del TA no se decodifica ni se dibuja (se sale antes de
`decoder.reset()`); el guest corre igual porque el video solo lee su estado. La caché de texturas está
vacía en el primer render dibujado, así que toda imagen se decodifica de la VRAM de ese momento. El
log reporta `dibujar_desde` en la línea `config:`. Las pruebas con captura usan
`N = cuadro de captura − 10` (`comparar-titulo.sh`); un `--cfg dibujar_desde=0` posterior vuelve a
dibujar todo, y `test-psp-texturas-juego.sh` lo pasa porque suma `over_budget` de toda la corrida.
Ahorra poco (≈4 s de 37): sin limitador el tiempo de pared ya lo pone la CPU del guest.

## `max_cuadros` y fin de la corrida

`test-psp-igual-pc.sh` escribe `max_cuadros=HASTA+1` para que el EBOOT pare al terminar, y
`scripts/probar-ikaruga-ppsspp.sh` termina en cuanto ve `max_cuadros reached` en el log (o cuando el
EBOOT sale, con `capturar_en`). `<segundos>` es solo una cota. Las corridas de una prueba van en serie:
dos PPSSPP a la vez rompen la regla de un solo PPSSPP y, con `xvfb-run -a`, comparten display y mueren
(el script usa `xvfb-run -d`).

## Las pruebas

Todas necesitan `PROBAR_DISCO` (el volcado del área HD real, `docs/primer-uso.md`); las que usan el
lanzador de PC (`igual-pc`) además `DREAM_RECOMP`.

```sh
export PROBAR_DISCO=<DISCO-real.ISO> DREAM_RECOMP=<dream-recomp>
E=build/psp/ikaruga/EBOOT.PBP J=$DREAM_RECOMP/games/ikaruga/extracted
HASTA=240 scripts/test-psp-igual-pc.sh $E $J     # arranque rápido igual a PC (10 s)
scripts/test-psp-titulo.sh $E $J                 # docs/titulo-psp.md
scripts/test-psp-geometria.sh $E $J              # docs/geometria-psp.md
scripts/test-psp-texturas-juego.sh $E $J         # docs/velocidad-psp.md, caché de texturas
scripts/test-psp-video.sh $E $J                  # docs/video-psp.md
scripts/bateria.sh $E $J [--double EBOOT-double.PBP] [--sin-velocidad]   # todas, una línea por prueba
# una captura a mano, sin limitador y sin dibujar hasta 2590:
mkdir -p build/c; printf 'capturar_en=2600\ndibujar_desde=2590\nrtc_semilla=1\n' > build/c/ikaruga.cfg
PROBAR_SIN_LIMITE=1 PROBAR_CFG=build/c/ikaruga.cfg scripts/probar-ikaruga-ppsspp.sh $E $J 2400 build/c/psp.log
```

Las pruebas de imagen comparan solo contra Flycast, en la imagen alineada con el cuadro de la PSP
(`scripts/flycast-alineado.sh`, `docs/referencia-flycast.md`); las imágenes se graban solas la primera
vez que faltan (necesita `FLYCAST_BIOS`). `bateria.sh` corre al integrar un cambio; salidas en
`build/psp/evidencia/bateria/`. `test-psp-texturas-juego.sh` comprueba (a) que la caché no rechace
ningún pedido en el render capturado, (b) PSNR ≥ 20 dB contra Flycast en el cuadro 2350 y (c) que con
`texturas_kb=2048` sí rechace.

## Estado guardado

Para saltarse el arranque en una prueba que mira el cuadro 2600: `docs/estado-guardado.md`.

## Qué no cubre

- `arranque`, `rtc`, `carpeta`, `vmu`, `salida-suspension` y `redondeo` corren con limitador.
- `dibujar_desde`: la captura idéntica se comprobó en el cuadro 2600; en otro cuadro una imagen que una
  caché de corrida completa conservara y que ya no coincide con la VRAM podría dar otra imagen (por
  diseño se decodifica de la VRAM del momento).
