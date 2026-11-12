> Foto de la medición del 2026-11-06; el documento vigente es [`../geometria-psp.md`](../geometria-psp.md).

# Fila 5c — geometría de Ikaruga en la PSP (medido en PPSSPP)

> **Advertencia:** números de **PPSSPP 1.20.4 (flatpak) sobre un Ryzen 9 9950X**, no de una PSP
> real. `host_s` es el reloj **emulado** de la PSP (`sceKernelGetSystemTimeWide`), no el del PC.
> Evidencia (capturas, logs, máscaras) en `build/psp/evidencia/`: el sandbox de esta corrida no
> dejó escribir en `/home/unjordi/code/ajenos/ikaruga-tareas/fila5c-evidencia/`. Las capturas son
> del juego y no van al repo.

## Qué hay

- `psp/stubs/video.cpp`: dibuja con sceGu el `dream::render::Frame` de cada render que el guest
  arranca (`pvr.on_render` en `psp/ikaruga/main.cpp`: `DisplayList::feed_stream` +
  `add_background`, como el lanzador de PC). Modo `GU_TRANSFORM_2D`, vértice
  `GU_COLOR_8888 | GU_VERTEX_32BITF`, escala 640×480 → 480×272 al convertir. Sin texturas: cada
  tira sale en su color base por vértice. Orden opaca → punch-through (alpha test con
  `PT_ALPHA_REF`) → translúcida ordenada de atrás hacia adelante por tira, con mezcla
  `sceGuBlendFunc` traducida de los campos SRC/DST del TSP y sin escribir z. z-buffer de 16 bit:
  el 1/w de cada cuadro (`min_z..max_z`) repartido en 1..65535, comparación del ISP traducida
  tal cual (mayor = más cerca, borrado a 0). Doble búfer en eDRAM: 2 × 512×272×4 + z 512×272×2
  (1,33 MB de 2 MB). Sin orientación TATE todavía.
- Captura: copia del búfer visible a RAM con `sceGuCopyImage` (leer la eDRAM directo desde la CPU
  dio una captura vacía en PPSSPP) y escritura RGB888 cruda.
- `ms0:/PSP/GAME/IKARUGA/ikaruga.cfg` (una clave por línea):
  `capturar_en=N` (tras dibujar el primer render en o después del cuadro N escribe
  `captura-N.raw` y sale, con `capture: … width 480 height 272 format rgb24` en el log),
  `max_cuadros=N`, `pulsar=start@700,…` (sintaxis de `--press`; 8 cuadros cada una),
  `rtc_seed=N` (como `--rtc-seed`; `rtc_semilla` es alias), `dibujar=0` (borra y presenta pero no dibuja),
  `profundidad=0`, `trazar=1` (una línea por tira del render capturado), `volcar_ta=1`
  (escribe además `ta-N.bin`, la corriente del TA de ese render).
- `scripts/comparar-geometria.sh` y `scripts/test-psp-geometria.sh` con las dos pruebas de
  aceptación (IoU contra Flycast y su contraprueba). `scripts/contar-poligonos.cpp` (decodifica
  una corriente de `--dump-ta` del PC con el `DisplayList` del submódulo) queda como herramienta
  de depuración a mano; ninguna prueba la usa.
- `scripts/probar-ikaruga-ppsspp.sh`: `PROBAR_CFG`, sale en cuanto el EBOOT termina y copia
  `captura-*.raw`/`ta-*.bin`. `scripts/build-psp-ikaruga.sh --solo-os`.

EBOOT medido: variante mix (runtime, stubs y `main.cpp` a -O2; generado a -Os), sha256
`fe4638c2e01ec5acea8ac4549a12126fef9080ba016b5eb273d0040f080653c2`, con idle-skip:
`scripts/build-psp-ikaruga.sh <dr-idle>/build-rel/games/ikaruga/gen --limpio` (4 min 23 s);
imagen 14,79 MB + guest 28,1 MB = 42,89 MB de ~52 MB. La -Os de la misma corrida
(`EBOOT-Os.PBP`): 14,70 MB + 28,1 MB = 42,80 MB.

Variables: `E=build/psp/ikaruga/EBOOT.PBP G=<dr-idle>/games/ikaruga/extracted`,
`PROBAR_DISCO=.../DISCO-real.ISO`.

## Prueba contra Flycast (cuadro 2350, inicio del capítulo 1)

La referencia es Flycast con el mismo disco (`docs/referencia-flycast.md`), no el PC recompilado.

```sh
scripts/test-psp-geometria.sh $E $G          # dos corridas de PPSSPP sin limitador, ~35 s cada una; --reusar repite sólo la comparación
```

`comparar-geometria.sh` captura el cuadro N en PPSSPP, toma de la serie de Flycast la imagen de
mayor SSIM dentro de `VENTANA_CAP1` (`scripts/escenas-flycast.sh`, índices 380-420) y compara las
máscaras de píxeles no negros (algún canal > 24; el color de borrado es negro). Umbral fijado
antes de medir: cobertura de la PSP > 20 % de la pantalla e IoU ≥ 0,6.

| | PSP | Flycast |
|---|---|---|
| píxeles fuera del negro | 54,36 % | 54,71 % (f392 de la serie) |
| IoU de las máscaras | **0,9798** | |

(b) con `dibujar=0` la captura es un solo color (negro) y la IoU es 0,0000: la prueba falla como
debe. Se pasa `--sin-borde` en esa corrida porque no se espera encontrar la escena.

El cuadro 2350 y no el 2600: Flycast corre por reloj de pared y en el 2600 la escena de la PSP
(estructuras en diagonal) no está en la serie en el mismo instante (mejor par 7,01 dB). En 2350 la
nave frente a la torre sí está (21,80 dB / SSIM 0,798 de pantalla completa).

## Tiempo de cuadro en PPSSPP (log del EBOOT, `video:` cada 60 cuadros)

| tramo | velocidad acumulada | tiempo medio de `on_render` (decodificar + dibujar), acumulado |
|---|---:|---:|
| título, cuadro 400 | 0,0409x | 0,18 ms |
| cuadro 1140 | 0,0323x | 0,20 ms |
| juego, cuadro 2580 | 0,0238x | 7,49 ms (6,72 ms con `dibujar=0`) |

- Con idle-skip y la variante mix (C++ de `dr-idle/build-rel/games/ikaruga/gen`): **837 s de
  reloj** con dibujo y 835 s con `dibujar=0`, las dos en paralelo (`psp-tiempo.txt`); la prueba
  entera, 14 min 45 s. Velocidad acumulada 0,1118x al cuadro 420, 0,1442x al 1140, 0,0525x al
  2580 y 0,0524x al 2600 (`host_s` 827,6 para 43,4 s del guest; 2 813 878 saltos de espera,
  6 076 744 960 ciclos saltados); `on_render` medio 5,67 ms con dibujo y 4,87 ms sin dibujo al
  cuadro 2580. Resultado igual al de la tabla: IoU 0,6068, 947 polígonos en los dos lados.
- Con idle-skip y todo a -Os: **904 s de reloj** con dibujo y
  902 s con `dibujar=0`, las dos en paralelo (`psp-tiempo.txt`); velocidad acumulada 0,0484x al
  cuadro 2600 y 7,50 ms de `on_render` medio. Las filas de arriba y lo que sigue son sin idle-skip.
- Llegar al cuadro 2600 sin idle-skip: **1828 s de reloj** en la corrida con `dibujar=0`
  (`geometria-2600-sin-dibujo/psp-tiempo.txt`); la corrida con dibujo da 1822 s de reloj PSP
  emulado en su log (su tiempo de reloj no quedó escrito: la corrí antes de que el script lo
  guardara). Con dibujo y sin dibujo, los ciclos del guest en cada `tick` son idénticos.
- La diferencia con/sin dibujo (≈0,8 ms de media acumulada) es lo que cuesta el GE en PPSSPP; la
  mayor parte del tiempo de `on_render` es decodificar en la CPU, que existe aunque no se dibuje.
- Fila 5a (sin video) al cuadro 420: 0,0402x (`docs/arranque-psp.md`); con video, 0,0409x al 400:
  en el título el render no es lo que limita en PPSSPP. En juego no tengo el número sin video.

## Por qué moría la corrida con dibujo
Dos corridas de PPSSPP en paralelo con `xvfb-run -a` (como lanzaba antes `test-psp-geometria.sh`; hoy van en serie): las dos
eligen el mismo display (`:99`, el primero sin `/tmp/.X99-lock` al momento de mirar); la que
termina primero mata ese Xvfb y la otra muere con `X connection to :99 broken` y
`terminate called without an active exception` (PPSSPP sale con 134). No es el EBOOT: el
`ikaruga.log` de la víctima se corta a media partida sin error. Cuál de las dos muere depende de
cuál termina primero; que el revisor viera morir siempre la de dibujo no lo medí.

Medido con 4 corridas cortas, dos a dos en paralelo (`build/psp/evidencia/repro-xvfb-a/`):
con `-a`, `a1` y `a2` caen en `:100` y `a2` muere (exit 134, «run failed»); con `-d` (Xvfb
elige display con `-displayfd`), `d1` en `:4` y `d2` en `:3`, las dos llegan a su captura.
`probar-ikaruga-ppsspp.sh` y `probar-ppsspp.sh` usan ahora `xvfb-run -d`, y el primero copia
`ikaruga.log`, el log y la salida de PPSSPP aunque la corrida falle, y escribe el código de
salida de PPSSPP.

Sospechas descartadas con medición, en la corrida con dibujo al cuadro 2600:
- Lista GE: el render capturado usa 34 668 bytes de 1 048 576 (`ge_list_bytes` en el log; 96
  con `dibujar=0`). Aun así `video_draw_frame` ahora corta el render (y cuenta `dropped`) antes
  de escribir más allá de `g_list`; en el cuadro 2600, `dropped 0`.
- `sceGuCopyImage` y la escritura de la captura: corrida sola (sin otra en paralelo), EBOOT
  previo al límite de la lista (sha256 `b93d72d4…`): `capture: captura-2600.raw` escrita y PPSSPP vivo al final.
- eDRAM: el mismo reparto de buffers sirve 2319 renders seguidos sin fallo en esa corrida.

## Determinismo
Con `rtc_seed=1` (lo que escriben los comparadores en `ikaruga.cfg`) la PSP no toma la hora real y
el render de un cuadro dado es el mismo en cada corrida.

## Qué NO verifiqué y dónde está mi punto ciego

- **La IoU es una medida gruesa.** El campo de juego ocupa casi la misma área en Flycast y en la
  PSP (columnas laterales negras en ambos), así que la IoU sale alta (0,98) aunque el interior
  difiera: en ese cuadro el centro está a 20,28 dB / SSIM 0,682. La prueba dice que la forma y la
  cobertura coinciden, no que el contenido sea igual.
- No comparé la mezcla de la translúcida contra Flycast (sin texturas el alfa de vértice casi siempre es
  1); los factores 2/3 («other colour») los traduje por lectura del GE, sin prueba.
- Comparé z sólo en un cuadro; la escala lineal del 1/w a 16 bit puede pelearse con superficies
  muy juntas en otras escenas. Recorte de tile del usuario, culling y modificadores: ignorados.
- Todo es PPSSPP: no corrí en una PSP real; ni el tiempo del GE ni el de la eDRAM se pueden
  sacar de aquí. No probé la presentación en pantalla (sólo la captura).
