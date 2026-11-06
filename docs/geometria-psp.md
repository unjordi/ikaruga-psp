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
- `scripts/comparar-geometria.sh`, `scripts/contar-poligonos.cpp` (decodifica una corriente
  de `--dump-ta` con el `DisplayList` del submódulo y da los mismos conteos que el log de la PSP;
  así no hizo falta parche en `patches/dream-recomp/`) y `scripts/test-psp-geometria.sh` con las
  tres pruebas de aceptación.
- `scripts/probar-ikaruga-ppsspp.sh`: `PROBAR_CFG`, sale en cuanto el EBOOT termina y copia
  `captura-*.raw`/`ta-*.bin`. `scripts/build-psp-ikaruga.sh --solo-os`.

EBOOT medido: sha256 `9d94e2b960898639e751ac51864602eb952876f7d9418e10b0a008cdb5ef926d` (con idle-skip),
`scripts/build-psp-ikaruga.sh <dr-rev>/build-rel/games/ikaruga/gen --solo-os`; imagen -Os
14,68 MB + guest 28,1 MB = 42,78 MB de ~52 MB (sube 1,0 MB sobre la fila 5a: libpspgu y la lista).

Variables: `E=build/psp/ikaruga/EBOOT.PBP G=<dr-rev>/games/ikaruga/extracted
B=<dr-rev>/build-rel/games/ikaruga/ikaruga_boot C=<dr-rev>/games/ikaruga/ikaruga.toml`.

## Resultado en el cuadro 2600

```sh
scripts/test-psp-geometria.sh $E $G $B $C          # ~16 min con idle-skip; --reusar repite sólo la comparación
```

| | PSP (PPSSPP) | PC |
|---|---|---|
| polígonos del `Frame` (sin el plano de fondo) | **947** (595 opacas con el fondo, 0 punch, 353 transl.) | **947** (594 opacas, 0 punch, 353 transl.) |
| palabras de la corriente del TA | 47 112 | 47 112 |
| píxeles fuera del negro (umbral 24) | 93,85 % | 56,95 % |
| IoU de las máscaras | **0,6068** (intersección 74 360, unión 122 535) | |
| geometría de PC que también está en la PSP | 100 % | |

```
(a) ok: geometry and IoU
(b) ok: without drawing the capture is one colour and the comparison fails
(c) ok: polygons psp 947 pc 947
```

(b): con `dibujar=0` la captura es un solo color (negro, 0,00 % de geometría, IoU 0) y
`comparar-geometria.sh` sale con 1. En la PSP «opacas 595» incluye el plano de fondo que agrega
`add_background`; el conteo comparado es el previo, igual al de `contar-poligonos` sobre
`ikaruga_boot --dump-ta-at-frame 2600`.

Capturas (descritas):
- **PC** (`--window --screenshot-at 2600`, escalada a 480×272): capítulo 1 en juego. Columna
  izquierda negra con «1P», «CHAIN STATUS», la columna de casillas de cadena y «x2»; centro con el
  terreno beige y estructuras metálicas en diagonal, la nave blanca arriba a la izquierda;
  columna derecha negra con «PLEASE WAIT».
- **PSP** (sin texturas): casi toda la pantalla blanca, porque los polígonos texturizados salen en
  su color de vértice (`ffffffff`), incluidos los de las columnas laterales y el texto del HUD;
  dos bandas verticales negras y sombreado gris donde el juego da color por vértice a las
  estructuras del centro. La forma coincide con la de PC (todo lo que PC pinta, la PSP lo cubre),
  pero no se reconoce el juego hasta que haya texturas.

### Umbral del fondo
La máscara toma como fondo el color de borrado de ambos renderers (negro). La primera versión
usaba el color más frecuente de cada imagen: en la PSP ése es el blanco de los polígonos
texturizados, y la IoU daba 0,1188 (`comparar-geometria.sh … --reusar` con esa versión).

## Tiempo de cuadro en PPSSPP (log del EBOOT, `video:` cada 60 cuadros)

| tramo | velocidad acumulada | tiempo medio de `on_render` (decodificar + dibujar), acumulado |
|---|---:|---:|
| título, cuadro 400 | 0,0409x | 0,18 ms |
| cuadro 1140 | 0,0323x | 0,20 ms |
| juego, cuadro 2580 | 0,0238x | 7,49 ms (6,72 ms con `dibujar=0`) |

- Con idle-skip (C++ de `dr-idle/build-rel/games/ikaruga/gen`): **904 s de reloj** con dibujo y
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
`test-psp-geometria.sh` lanza las dos corridas de PPSSPP en paralelo. Con `xvfb-run -a` las dos
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
Sin `rtc_semilla` la PSP tomaba la hora real y el render del cuadro 400 difería del de PC con
`--rtc-seed 1`. Además, `--vmu v.vmu` en PC crea una tarjeta formateada vacía, mientras la PSP
corre sin tarjeta en ms0: (una en blanco sólo en memoria): con `--vmu` PC daba 21 polígonos en el
cuadro 400 y la PSP 25; sin `--vmu`, 25 y 25. `comparar-geometria.sh` corre PC sin `--vmu`.

## Qué NO verifiqué y dónde está mi punto ciego

- **La IoU pasa casi por construcción.** Sin texturas la PSP cubre el 94 % de la pantalla; una
  máscara casi llena contra la de PC (57 %) da IoU ≈ 0,57–0,61 aunque la posición fuera mediocre.
  0,6068 está a 0,007 del umbral. La prueba dice que no falta geometría (100 % de la de PC está
  en la PSP) pero mide mal si sobra o si está desplazada. Una comparación mejor sería contra un
  render de PC sin texturas o por polígono, que no hice.
- No comparé la mezcla de la translúcida contra PC (sin texturas el alfa de vértice casi siempre es
  1); los factores 2/3 («other colour») los traduje por lectura del GE, sin prueba.
- Comparé z sólo en un cuadro; la escala lineal del 1/w a 16 bit puede pelearse con superficies
  muy juntas en otras escenas. Recorte de tile del usuario, culling y modificadores: ignorados.
- El render capturado es el 2319.º de la PSP y el 2320.º de PC en el mismo cuadro (misma
  corriente de 47 112 palabras): no averigüé de dónde sale el render de diferencia en el conteo.
- Todo es PPSSPP: no corrí en una PSP real; ni el tiempo del GE ni el de la eDRAM se pueden
  sacar de aquí. No probé la presentación en pantalla (sólo la captura).
- La PSP corre con una tarjeta en blanco formateada en memoria; PC sin `--vmu` no conecta tarjeta.
  Los conteos coinciden así, pero no verifiqué que el juego vea lo mismo en ambos casos.
