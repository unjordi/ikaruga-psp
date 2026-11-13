# Estado verificado de los pasos cerrados

Revisión de cada paso ✅ de `docs/plan.md` con el protocolo de pruebas del proyecto: qué prueba lo cerró, si mide lo que ve el jugador (pantalla, fps, memoria) o un modelo, si su contraprueba falla contra el código viejo y si el cierre se puede relatar sin matices. Veredictos:

- **sostiene**: la evidencia se repitió sobre develop (comando y salida abajo) y mide el efecto.
- **reabierto**: el paso volvió a ⬜ en el plan con lo que le falta.
- **sin evidencia**: el cierre puede ser cierto pero su prueba no lo demuestra; va a la revisión de las pruebas.

Develop de referencia: `d7dc10e`. Build `float` (el de entrega) salvo donde dice `double`, disco real, BIOS del usuario, PPSSPP sin limitador salvo la velocidad.

## Oráculo de imagen

Las pruebas de imagen (`test-psp-titulo`, `test-psp-video`, `test-psp-texturas-juego`) comparaban la captura de la PSP contra la mejor imagen de una ventana de 30–40 cuadros de la serie de Flycast grabada por reloj de pared. Esa serie no muestra el mismo instante que la PSP (paso «Desfase de la línea de tiempo»), y elegir la mejor de una ventana ancha deja pasar una escena que no coincide. Ahora comparan contra las imágenes de Flycast alineadas con el cuadro de la PSP (`scripts/flycast-alineado.sh`: N+DESFASE−2 … N+DESFASE+2, desfases medidos en `scripts/escenas-flycast.sh`; sin desfase medido la prueba sale con 2). Flycast por cuadro es determinista: el 2333 grabado dos veces sale idéntico byte a byte. Desfase del título con `start@700` medido: +6 (Flycast 1225–1255 contra la PSP 1243: el fundido termina en el 1249, 29,51 dB).

## Batería sobre develop

`scripts/bateria.sh EBOOT G --double EBOOT-double`, con las pruebas de imagen alineadas: 15/15.

| prueba | medido | contraprueba |
|---|---|---|
| igual-pc-double | `write_hash` igual al PC en cada cuadro 1…2600 con las pulsaciones del capítulo 1 | (`--contraprueba` sin DAZ diverge en 1983, ver paso 18; no repetida en esta revisión) |
| igual-pc (float) | igual al PC en 60…600 | — |
| título (a) | 29,51 dB / 0,961 contra Flycast 1249 | sin texturas 9,47 dB; contra SELECT alineado 9,24 dB |
| video (a) recorte, 2350 | columnas 0,984 / 0,998 | `recorte=0`: 0,116 / 0,085 |
| video (b) niebla, 1750 | 31,34 dB / 0,930, botones 28,35 dB | `niebla=0`: botones 15,86 dB |
| texturas-juego (b), 2350 | 27,41 dB | (c) ver abajo |
| velocidad, 2600 | 0,0846x | — |
| memoria, 2600 | bloque libre mayor 928 KB, en uso 35,77 MB | — |

## Pasos

| paso | veredicto | evidencia de esta revisión |
|---|---|---|
| 1–6 | sostiene | elección de recompilador, receta, toolchain, tamaño, medición en PC, 333 MHz: no se re-midieron; son decisiones o mediciones de partida, no cierres de una prueba |
| 7 arranque | sostiene | `test-psp-arranque` ok (5 PC distintos del guest); `igual-pc` 60…600 |
| 8, 32 idle-skip | sostiene | `igual-pc-double` 1…2600 y velocidad 0,0846 en develop |
| 9 video mínimo | sostiene (re-medido) | se había cerrado contando polígonos contra el PC; contra Flycast alineado: título 29,51 / 0,961, 1750 31,34 / 0,930, 2350 27,41 / 0,956; `dibujar=0` da un solo color |
| 10 texturas | sostiene | título contra Flycast 29,51 dB; sin texturas 9,47 |
| 11, 18 correctitud | sostiene | `igual-pc-double` 1…2600; `redondeo`, `rtc`, `carpeta`, `vmu`, `salida-suspension` ok |
| 12 disco real | sostiene | todas las corridas usan `DISCO-real.ISO` |
| 13 velocidad | sin evidencia (parcial) | 0,0846x medido; el presupuesto de 2,5 MB de la caché se justifica con un contador, no con la imagen (ver texturas-juego) |
| 14 recorte y niebla | sostiene | video (a) y (b) arriba, con contraprueba |
| 15 primer uso | pendiente | el recorrido de punta a punta desde un directorio limpio va al final de esta revisión |
| 16 gitignore | sostiene | `test-gitignore` ok |
| 17, 42 herramientas Flycast | sostiene | Flycast por cuadro determinista |
| 19 controles | reabierto | sin verificar: R y A+B soltando la energía, `tate=2`, el umbral del stick |
| 20, 21 sin limitador, `dibujar_desde` | sostiene | toda la batería corre sin limitador; la captura del 2600 sale idéntica en tres corridas distintas (`cb37591…`) |
| 22–25 estado guardado | sostiene | volcado en 2590 y carga: `hash` 2591…2600 iguales a la corrida sin volcado y `captura-2600.raw` idéntica; volcar no altera la corrida (2601 `hash` iguales); con un byte alterado cada 4 KB del volcado, 9 de los 10 cuadros salen distintos. Dos bytes alterados en un solo lugar no cambiaron nada (memoria que esos 10 cuadros no leen). No hay prueba en el repo: es receta manual (`docs/estado-guardado.md`, que usaba la clave vieja `hash_every`, ya corregida) |
| 26 pulsación guionada | sostiene | medición sin cambio de código; no se repitió |
| 27–30 herramientas de prueba | sostiene | la batería las usa (falta de `PROBAR_DISCO` sale con 2, `bateria.sh`, build mix, socket de Wayland) |
| 31 imagen solo contra Flycast | reabierto y cerrado aquí | comparaba contra la serie por reloj; ahora contra Flycast alineado |
| 33 RAM rápida | sostiene | `igual-pc-double` 1…2600 |
| 34 memoria | sostiene | 928 KB de bloque libre mayor en el 2600, por debajo del margen de 2 MB (riesgo abierto) |
| 35, 36 limpieza | sostiene, con un hallazgo | quedaban claves viejas en `docs/estado-guardado.md` y `docs/pruebas-rapidas.md` (`hash_every`, `rtc_seed`), ya corregidas |
| 37 escena por escena | sostiene | su tabla era contra la serie por reloj; las escenas que usan las pruebas se re-midieron alineadas |
| 38 leyenda | sostiene (re-medido) | texto quieto, igual en cualquier cuadro cercano de Flycast: menú 1300 21,75 / 0,931, pausa 2050 20,91 / 0,876; sin `DC_BOOT.BIN` 12,64 / 0,357 y 11,67 / 0,279. El costo de velocidad pasó a paso propio |
| 39 HUD | sostiene | columnas del 2350 0,984 / 0,998 contra Flycast alineado |
| 40, 41 perspectiva | sostiene (re-medido) | el SSIM 0,247 del 2600 era desfase. `perspectiva=1` frente a `0`: 2300 nubes 29,33/0,907 frente a 23,10/0,626; 2600 centro 21,16/0,783 frente a 19,81/0,757; 2700 nubes 30,91/0,943 frente a 19,39/0,628. A ojo, el 2600 es la misma escena con el cielo más claro y con menos contraste que en Flycast |
| 43 desfase | sostiene | las pruebas lo usan; título medido (+6) |
| 44 mipmaps | sostiene | 2300 `ala` 28,36/0,927 frente a `mipmaps=0` 24,24/0,844 (iguales a su cierre) |
| 45 profundidad | cerrado en esta revisión | imagen contra Flycast alineado y a ojo (cifras en el plan); `analizar-ta` describe el reparto, no la pantalla |
| 46 perspectiva de texturas | sostiene | 2300 `cielo-sup` 32,81/0,956 |

## Pruebas que no prueban lo que dicen

- `test-psp-texturas-juego` (b)/(c): con `texturas_kb=2048` la captura del 2350 sale idéntica byte a byte a la normal; los 10 907 rechazos caen en otros cuadros. La (c) solo comprueba que un contador sube; ninguna prueba muestra que el presupuesto cambie lo que se ve.
- `test-psp-geometria`: la IoU de máscaras no negras pasa con cualquier escena que tenga geometría en el mismo lugar; sigue contra la serie por reloj.
- `analizar-ta` (paso 45) aplica la fórmula de `video.cpp` al stream del TA, que es el mismo con o sin el cambio: mide un modelo del cálculo, no la pantalla. El paso 45 se mide con la imagen contra Flycast.
- El estado guardado no tiene prueba en el repo.
