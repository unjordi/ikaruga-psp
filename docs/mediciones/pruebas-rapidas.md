> Foto de la medición del 2026-11-11; el documento vigente es [`../pruebas-rapidas.md`](../pruebas-rapidas.md).

# Pruebas rápidas en PPSSPP (fase 1.5)

*Números de PPSSPP 1.20.4 (flatpak) en el host de desarrollo, no de una PSP real. `wall_s` es el
reloj de pared del host; `host_s` es el reloj emulado de la PSP (`sceKernelGetSystemTimeWide`).*

Objetivo: que las pruebas de imagen y de correctitud tarden menos en reloj de pared sin cambiar
lo que miden. `scripts/medir-velocidad.sh` no cambia: sigue con el limitador de 60 Hz de PPSSPP,
igual que antes.

## 1. PPSSPP sin limitador: `PROBAR_SIN_LIMITE=1`

**Cómo lo hace PPSSPP 1.20.4** (fuente de la etiqueta `v1.20.4` de hrydgard/ppsspp):

- `Core/HLE/sceDisplay.cpp`, `FrameTimingLimit()`: devuelve 0 (sin límite) si
  `PSP_CoreParameter().fastForward` es verdadero; si no, el límite de `FPSLimit::CUSTOM1/2/ANALOG`
  o 60.
- `UI/EmuScreen.cpp` (~761-770): `VIRTKEY_FASTFORWARD` pone `fastForward = true` mientras la
  tecla está abajo y lo vuelve a `false` al soltarla.
- `Core/KeyMapDefaults.cpp:43`: `VIRTKEY_FASTFORWARD` es `Tab` en el mapa de teclado por omisión.
- No hay clave de `ppsspp.ini` que arranque sin límite: `FrameRate` (`iFpsLimit1`, `Core/Config.cpp`)
  solo se aplica cuando el usuario activa `CUSTOM1` con una tecla, y `PPSSPPSDL` no tiene opción de
  línea de órdenes para esto (`UI/NativeApp.cpp`: `--log`, `--state`, `--appendconfig`, ...).

Por eso `scripts/probar-ikaruga-ppsspp.sh` con `PROBAR_SIN_LIMITE=1` mantiene `Tab` abajo con
`xdotool keydown Tab` dentro del Xvfb, una vez por segundo (una pulsación antes de que exista la
pantalla del juego se pierde; repetirla no hace daño). Necesita `xdotool` en el host.

El tiempo emulado no cambia: el juego, sus ciclos y `host_s` son los mismos; solo deja de esperar
al vsync del host. Por eso **no** vale para medir velocidad.

| Corrida (EBOOT de este commit, disco real, `rtc_seed=1`) | con limitador | `PROBAR_SIN_LIMITE=1` |
|---|---|---|
| `capturar_en=600` | 89 s | 12 s |
| `capturar_en=2600` + pulsaciones de `medir-velocidad.sh` | 531 s | 36 s |

Lo medido no cambia (cuadro 2600, ambas corridas):

- línea `capture written`: `cycles 8679556491 guest_s 43.398 host_s 522.985` en las dos;
- `captura-2600.raw`: sha256 `99dd45234e61b9b1…` en las dos;
- `write_hash` del cuadro 600: `hash 600 2003507820 2986fff765029c09 39937071 rtc 631152011` en las
  dos; el sha256 de todas las líneas `hash` del log (cuadros 0…2600): `72ac22c97e7a32d7…` en las dos.

## 2. `dibujar_desde=N` en `ikaruga.cfg`

`psp/ikaruga/main.cpp`: antes del cuadro de guest `N` el render de TA no se decodifica ni se
dibuja (se sale antes de `decoder.reset()`); el guest corre igual porque el video solo lee su
estado. La caché de texturas está vacía en el primer render dibujado, así que toda imagen se
decodifica de la VRAM de ese momento; solo cambian los contadores de la caché. La línea `config:`
del log reporta `dibujar_desde`. Las pruebas con captura usan `N = cuadro de captura − 10`.

| `capturar_en=2600`, `PROBAR_SIN_LIMITE=1` | `wall_s` | `host_s` emulado |
|---|---|---|
| sin `dibujar_desde` | 37 s | 522.983 |
| `dibujar_desde=2590` | 33 s | 480.943 |

Captura `99dd45234e61b9b1…` y secuencia de `write_hash` `72ac22c97e7a32d7…` idénticas en las dos.
El ahorro es pequeño (~4 s de 37): sin limitador el tiempo de pared ya lo pone la CPU del guest,
y el dibujo es ~8 % del tiempo emulado.

## 3. Estado guardado: dream-recomp no lo trae

Busqué en `third_party/dream-recomp` (commit `51ca276`) `savestate|save_state|serializ|snapshot|
load_state`: no hay nada (el único resultado, `Arm7::switch_mode(int, bool save_state)`, es el SPSR
del ARM7). Tampoco en `patches/` ni `psp/`. No lo implementé. Lo que haría falta:

**Por qué no es solo «volcar la RAM».** El guest es código SH4 traducido a C++ que corre sobre la
pila del host: en mitad de un cuadro el estado incluye marcos de C++ de funciones traducidas.
`run_guest` (`runtime/src/sh4/abi_bare.cpp`) ya sabe reanudar en una dirección (`resume_at`, con las
entradas `resume` de `FunctionEntry`, `sh4/abi.h`) tras un `NonLocalReturn`. El punto de volcado
tendría que ser uno donde la pila del host no tenga marcos del guest vivos que importen, o cargar
el estado como un `NonLocalReturn` a `ctx.pc`; eso exige que haya una etiqueta de reanudación en
ese PC (hoy solo las hay donde el traductor vio retornos no locales). Lo más seguro: volcar en el
`on_vblank_out` cuando el guest está en su bucle de espera (idle-skip, `pc 0x0c03e01c` en los logs)
y comprobar que esa dirección tiene entrada de reanudación.

**Piezas de estado** (cabeceras de `runtime/include/dream/runtime/`, 2 504 líneas en total):

| Pieza | Dónde | Tamaño / dificultad |
|---|---|---|
| CPU SH4: registros, bancos, FPSCR, MAC, PR, PC | `sh4/ctx.h` | ~20 campos POD, fácil |
| RAM 16 MB, VRAM 8 MB, ARAM 2 MB, OCRAM 8 KB, flash 128 KB | `mem/dc_memory.h` | ~26 MB por volcado; épocas de VRAM (parche 0007) |
| Planificador: reloj y plazos | `sched/scheduler.h` | `std::function` no se serializa: guardar `now_` y `deadline` por evento por **nombre**, reconstruir los callbacks registrando el sistema igual y luego reponer plazos |
| SPG (cuadros, vblank) | `pvr/spg.h` | pocos campos + 1 callback (`on_vblank_out`, lo pone `main.cpp`) |
| PVR/TA: registros, FIFO/estado del TA, listas | `pvr/core.h` | 17 campos + 3 callbacks; el buffer del TA a medio cuadro |
| AICA + ARM7 + RTC + mezclador | `aica/*.h` | registros de canales, ARM7 (12 campos + bancos), RTC |
| GD-ROM (HLE): comando en curso, sector, estado de DMA | `hle/bios.h`, `gdrom/disc.h` | 16 campos; el `Disc` se reabre del ISO, no se guarda |
| Maple/VMU | `maple/maple.h` | 18 campos; VMU.BIN ya está en archivo |
| TMU, DMAC, INTC SH4, INTC/G2-DMA/sysblock de Holly | `sh4/tmu.h`, `sh4/dmac.h`, `sh4/intc.h`, `holly/*.h` | pequeños, POD salvo 1-2 callbacks |
| Host: guion de pulsaciones, RNG del RTC, estado de idle-skip | `psp/ikaruga/main.cpp`, `psp/stubs` | pocos campos |

Estimación: un `save(Writer&)`/`load(Reader&)` por pieza (~12 archivos del runtime + `system.h`/
`system.cpp` para orquestarlos, ~600-900 líneas), un parche nuevo en `patches/dream-recomp/` (o
subirlo a dream-recomp), un cargador en `main.cpp` (`cargar_estado=archivo` en `ikaruga.cfg`) y una
prueba de igualdad: correr de 0 a 2600 y desde un estado del cuadro 600 a 2600 y comparar las
líneas `write_hash` 601…2600. Lo difícil y no medido es el punto de reanudación seguro y que los
callbacks del planificador se registren siempre en el mismo orden. Un estado ocupa ~26 MB
(comprimido mucho menos; RAM de la PSP: no cabe junto al juego en 64 MB sin volcar por partes).

## 4. Las pruebas

Cambios:

- `scripts/comparar-titulo.sh` y `scripts/comparar-geometria.sh` (los usan `test-psp-titulo`,
  `-video`, `-texturas-juego` y `-geometria`): `PROBAR_SIN_LIMITE=1` por omisión
  (`PROBAR_SIN_LIMITE=0` lo apaga) y `dibujar_desde=<cuadro − 10>` en el `ikaruga.cfg`; un
  `--cfg dibujar_desde=0` posterior vuelve a dibujar todo.
- `scripts/test-psp-texturas-juego.sh`: pasa `--cfg dibujar_desde=0`, porque su check (a) suma
  `over_budget` de toda la corrida y la caché tiene que ver todos los renders (solo usa el paso 1).
- `scripts/test-psp-geometria.sh`: sus dos capturas ya no corren en paralelo (dos PPSSPP a la vez
  rompen la regla de un solo PPSSPP); en serie y sin limitador tardan menos que antes en paralelo.
- `scripts/test-psp-igual-pc.sh`: `PROBAR_SIN_LIMITE=1` por omisión y `max_cuadros=HASTA+1`, para que
  el EBOOT pare al terminar en vez de agotar los `SEGUNDOS` (antes la corrida duraba siempre la cota
  completa: el EBOOT no paraba). `scripts/probar-ikaruga-ppsspp.sh` ahora también termina al ver
  `max_cuadros reached` en el log.
- `scripts/medir-velocidad.sh`: fuerza `PROBAR_SIN_LIMITE=0`; mide igual que antes.

Tiempos (una corrida de cada prueba con el cambio, EBOOT de esta rama, sin otro PPSSPP corriendo
al arrancar cada una). «Antes» por corrida de PPSSPP son los `psp-tiempo.txt` de la fila 9b
(`ikaruga-psp-wt/f9b`, mismo código salvo este cambio) y, para `test-psp-video`, de
`ikaruga-psp-wt/f24`; no volví a correr las pruebas viejas con limitador.

| Prueba | PPSSPP por corrida antes | después | prueba entera después | resultado |
|---|---|---|---|---|
| `test-psp-titulo.sh` (2 corridas PSP + PC) | 149 s | 15-16 s | 199 s | (a) 28.43 dB, (b) 9.46 dB, (c) 7.37 dB: pasa |
| `test-psp-geometria.sh` (2 corridas, cuadro 2600) | 506 / 499 s (en paralelo) | 34 / 33 s (en serie) | 339 s | (a)(b)(c) ok, polígonos 947 = 947 |
| `test-psp-texturas-juego.sh` (2 corridas, 2600) | 506 / 508 s | 36 / 37 s | 205 s | PASS (a), (b) 17.92 dB (INFO), PASS (c) 10907 rechazos |
| `test-psp-video.sh` (5 corridas: 2600×2, 1700×2, 1243) | 868, 869, 262, 261, 149 s (f24) | 34, 33, 19, 19, 16 s | 404 s | (a) 25.64/5.16 dB, (b) 27.41/15.81 dB, (c) 28.43 dB: pasa |
| `test-psp-igual-pc.sh` `HASTA=240` (arranque rápido) | ~330 s (cota `SEGUNDOS`; 338 s medidos sin limitador y sin el corte por `max_cuadros`) | 10 s | 10 s | igual a PC en 60..240 |
| `test-psp-igual-pc.sh` `HASTA=600` | ~330 s | 13 s | 13 s | igual a PC en 60..600 |

El resto del tiempo de cada prueba de imagen es el lado de PC (ventana Wayland, `--screenshot-at`)
y la comparación, que este cambio no toca.

Comandos:

```sh
export PROBAR_DISCO=.../DISCO-real.ISO DREAM_RECOMP=.../dream-recomp
E=build/psp/ikaruga/EBOOT.PBP J=$DREAM_RECOMP/games/ikaruga/extracted
B=$DREAM_RECOMP/build-rel/games/ikaruga/ikaruga_boot T=$DREAM_RECOMP/games/ikaruga/ikaruga.toml
HASTA=240 scripts/test-psp-igual-pc.sh $E $J        # arranque rápido igual a PC
scripts/test-psp-titulo.sh $E $J $B $T
scripts/test-psp-geometria.sh $E $J $B $T
scripts/test-psp-texturas-juego.sh $E $J $B $T
scripts/test-psp-video.sh $E $J $B $T
# una captura a mano, sin limitador y sin video hasta 2590:
mkdir -p build/c; printf 'capturar_en=2600\ndibujar_desde=2590\nrtc_seed=1\n' > build/c/ikaruga.cfg
PROBAR_SIN_LIMITE=1 PROBAR_CFG=build/c/ikaruga.cfg scripts/probar-ikaruga-ppsspp.sh $E $J 2400 build/c/psp.log
```

## `docs/velocidad-psp.md` ítem por ítem

Esta fila no cambia la medición de velocidad: `medir-velocidad.sh` fuerza `PROBAR_SIN_LIMITE=0` y
no escribe `dibujar_desde` (queda en 0: la salida temprana del video nunca se toma). Lo comprobé con
una corrida de `medir-velocidad.sh --veces 1` del EBOOT de esta rama, con `PROBAR_SIN_LIMITE=1`
exportado a propósito (`build/ev-vel/`, 2026-11-11):

- juego 2600: speed 0,0830, host_s 522,983, draw_ms_per_render 17,76, **wall_s 531** (≈ host_s:
  el limitador sigue puesto aunque el llamador exporte `PROBAR_SIN_LIMITE=1`);
- título 1243: speed 0,1458, host_s 142,406, draw 0,26 ms, **wall_s 149**.

El host_s del juego antes de tocar `main.cpp` (paso 1, EBOOT de develop sin cambios) era 522,985;
con `dibujar_desde` compilado, 522,983: la rama nueva no cuesta nada medible.

| ítem de `velocidad-psp.md` | ¿lo toca esta fila? | evidencia hoy (EBOOT de esta rama) |
|---|---|---|
| Cómo se mide (comando, 2 casos, `perf:`) | no: mismo comando, mismos casos | `medir-velocidad.sh` solo gana `PROBAR_SIN_LIMITE=0` explícito |
| Criterio ≥ 3 % en dos corridas | no aplica: no es un cambio de velocidad | — |
| Línea base juego 2600 (develop adc3ddd), corridas 1 y 2 | no: EBOOT histórico, no remedido | hoy 0,0830 (otro EBOOT) |
| Línea base título 1243, corridas 1 y 2 | no: EBOOT histórico | hoy 0,1458 |
| «medida determinista» | sí la usa esta fila: es lo que permite comparar con/sin limitador | host_s 522,985 con y sin limitador (paso 1) |
| 0,0405 vs 0,0524 de geometría | no aplica (historia de discos) | — |
| draw_ms_per_render es promedio acumulado | sí importa: con `dibujar_desde` el promedio cubre menos renders | no se usa en ninguna prueba de esta fila |
| Cambio 1 A9 contadores | no lo toco; la línea `perf:` sigue | presente en `build/ev-vel/*/psp.log` |
| Tramo título 300–1380 (host_s/60 cuadros) | no | 5,6–6,7; ftrv 3, fmac 2–154, fipr 0 |
| Tramo juego 1500–1860 | no | 12,6–12,9; fipr 2 334, ftrv 5 197, fmac ~7 500, rechazos 0, draw 13–15 ms |
| Tramo juego 2040–2600 | no | 14,7–25,8 |
| throw por cuadro | no | 3–8 |
| scheduler por cuadro | no | 1 525–1 598 |
| Orden decidido (caché, A3, …) | no aplica | — |
| Cambio 2: develop | no: histórico | — |
| Cambio 2: solo páginas sucias | no: la época de VRAM avanza solo en renders dibujados; con `dibujar_desde` las páginas quedan marcadas y la caché, vacía, decodifica todo igual | captura 2600 idéntica (paso 2) |
| Cambio 2: páginas sucias + espacio primero | no | over_budget 0 |
| Título con páginas sucias 0,1458 | no | hoy 0,1458 |
| Rechazos 10 907 con 2 MB | no | `test-psp-texturas-juego` (c) da 10 907 otra vez, con `dibujar_desde=0` |
| Cambio 3 A3 entero (revertido) | no aplica | — |
| Cambio 4 `cache2` / `nobios` | no; por eso `texturas-juego` no usa `dibujar_desde` | over_budget 0, peak_demand 2 445 440, decode_ms 1 313,7 |
| Cambio 5 A5 (revertido) | no aplica | — |
| Diagnóstico `diagfloat` | no aplica | — |
| 9b: parches 0009 / PC 0002 | no los toco | — |
| 9b: `-ffp-contract=off -frounding-math` | no los toco | — |
| 9b: VFPU no usada | no aplica | — |
| 9b: `gen/` no cambia | tampoco con esta fila (solo `main.cpp`) | — |
| 9b: tabla nobios / float / vaciado de denormales | no remedido salvo float | float hoy 0,0830 (ver abajo) |
| 9b: contadores al cuadro 2600 | no | fipr 4 526, ftrv 5 927, fmac 10 893: iguales |
| 9b: igualdad (a) sin entrada 60…600 | sí: `test-psp-igual-pc` cambió | PASA en 13 s (`HASTA=600`) y en 10 s (`HASTA=240`) |
| 9b: (a') `PULSAR` hasta 1980, (a'') 2400, (b), (b'), control double | no corridos en esta fila | el nuevo `max_cuadros=HASTA+1` aplica igual |
| 9b: (c) título `--todas` | (a)(b)(c) corridos, `--todas` no (lo corre el orquestador) | 28,43 / 9,46 / 7,37 dB |
| 9b: (e) las 8 de `--todas` | no corridas salvo igual-pc y geometría | geometría: polígonos 947 = 947 |
| 9b: (d) `medir-velocidad --solo 2600` 0,0869 | remedido | 0,0830 (ver abajo) |
| 9b: divergencia de 1983 (denormales) | no aplica (regla de la fila: no se persigue en `float`) | — |
| Cambio 6 A6a SPG (revertido) | no aplica | — |
| A13 excepciones | no aplica | — |
| Resultado (a) juego 2600 | no | 0,0830 |
| Resultado (b) título `--todas` | parcial | ver arriba |
| Resultado (c) texturas `--psnr-ref 10.35` | corrida sin `--psnr-ref` | PASS (a) y (c); PSNR 17,92 dB |
| RAM libre develop / cache2 / final | no; el cambio no reserva memoria | juego 2600: in_use 35 053 624, bloque mayor 1 088 KB, kernel 764 KB |
| Qué NO verifiqué: hardware real | sigue igual | todo es PPSSPP |
| — velocidad del título final | medida otra vez | 0,1458 |
| — PSNR 2600 sin revisar a ojo | sigue | no miré las capturas a ojo |
| — `DREAM_NO_BIOS_IMAGE` | sigue supuesto | — |
| — costo de los contadores A9 | sigue sin medir | — |
| — páginas sucias solo por `resolve` | sigue | — |
| 9b: un solo `medir-velocidad`, carga del host | igual: una corrida | — |
| 9b: imagen float vs Flycast | no revisado | — |
| 9b: `build-psp-ikaruga.sh` reusa `obj/gen` | me afecta poco: solo cambié `main.cpp` (objeto de runtime, se recompila) | `strings` del ELF tiene `dibujar_desde` |

Filas de cada tabla de `velocidad-psp.md` (EBOOT histórico = construido en otra fila, no lo
reconstruí; esta fila no cambia ningún EBOOT salvo la clave `dibujar_desde`, que vale 0 al medir):

| tabla / fila | ¿aplica a esta fila? | evidencia |
|---|---|---|
| Línea base: juego 2600 corrida 1 / corrida 2 (0,0405) | no, EBOOT histórico | hoy una corrida: 0,0830, host_s 522,983 |
| Línea base: título 1243 corrida 1 / corrida 2 (0,1453) | no, histórico | hoy 0,1458, host_s 142,406 |
| A9: título 300–1380 / juego 1500–1860 / juego 2040–2600 | no | ver tabla anterior |
| Cambio 2: develop (0,0405) | no, histórico | — |
| Cambio 2: solo páginas sucias `velocidad-dirty` (0,0409) | no, histórico | — |
| Cambio 2: páginas sucias + espacio primero `velocidad-cache2` (0,0517) | no, histórico | — |
| Cambio 3: caché `velocidad-cache2` (0,0517) | no, histórico | — |
| Cambio 3: caché + A3 entero `velocidad-exact2` (0,0516) | no: revertido, no existe en el árbol | — |
| Cambio 4: caché 2 MB `cache2` (over_budget 10 907) | sí como contraprueba: `texturas_kb=2048` en `test-psp-texturas-juego` (c) | 10 907 rechazos, PPSSPP 37 s |
| Cambio 4: caché 2,5 MB sin búfer de ROM `nobios` (over_budget 0) | sí: es el default que corre todo | over_budget 0 en `build/ev-vel/juego-1` y en texturas-juego (a) |
| Cambio 5: `velocidad-nobios` (0,0518) | no, histórico (double) | — |
| Cambio 5: + memo `velocidad-a5` (0,0528) | no: revertido | — |
| Diagnóstico: `velocidad-nobios` | no, histórico | — |
| Diagnóstico: `float` `velocidad-diagfloat` (0,0902) | no: código no conservado | — |
| 9b: `velocidad-nobios` (fila 9) | no, histórico | — |
| 9b: float (parche 0009) 0,0869 | sí, es el árbol actual | hoy 0,0830 (explicación abajo) |
| 9b: float + vaciado de denormales por software (0,0834) | no: revertido | — |
| 9b igualdad (a): EBOOT float vs PC float, sin entrada, 60…600 | sí: es `test-psp-igual-pc.sh` por omisión, que cambió | PASA 10 cuadros, 13 s (`HASTA=600`); `HASTA=240` PASA 4 cuadros, 10 s |
| 9b igualdad (a'): `PULSAR` capítulo 1, `HASTA=1980` | aplica (mismo script), no corrida | — |
| 9b igualdad (a''): `HASTA=2400` (falla en 1983) | no se persigue (divergencia `float`, regla de la fila); no corrida | — |
| 9b (b): contraprueba EBOOT float vs PC double, `HASTA=1980` | requiere un PC double; no corrida | — |
| 9b (b'): EBOOT float vs PC double sin entrada | no corrida (no discrimina, según 9b) | — |
| 9b control: EBOOT double vs PC double `HASTA=2400` | es la prueba de igualdad del port (`DREAM_FPU_DOUBLE=1`); no corrida | — |
| 9b (c): `test-psp-titulo.sh --todas` | (a)(b)(c) sí; `--todas` lo corre el orquestador | 28,43 / 9,46 / 7,37 dB |
| 9b (e): las 8 de `--todas` | igual-pc y geometría sí; arranque, redondeo, rtc, carpeta, vmu, salida-suspension no | igual-pc PASA; geometría 947 = 947 |
| 9b (d): `medir-velocidad --solo 2600` | sí (remedido) | 0,0830 |
| A6a: `velocidad-nobios` / + SPG `velocidad-a6` | no: revertido | — |
| Resultado (a), (b), (c) de la fila 9 | (a) remedido 0,0830; (b) parcial como 9b (c)/(e); (c) sin `--psnr-ref` | PASS (a) y (c), PSNR 17,92 dB |
| RAM: develop / `cache2` / final `nobios` | no, histórico; esta fila no reserva memoria | hoy in_use 35 053 624, bloque mayor 1 088 KB, kernel 764 KB |

**0,0830 frente al 0,0869 de la fila 9b.** No lo causa esta fila: el EBOOT de develop sin mis
cambios (paso 1) ya daba host_s 522,985. Lo que cambió es el dibujo (draw_ms/render del 2600: 26,38
hoy contra 11,37 en 9b). Puede venir del EBOOT `eboot-noftz` de 9b, armado sobre otro árbol (quizá sin
la niebla en dos pasadas de las filas 6b/6c), pero no lo investigué.

## Qué NO verifiqué y dónde está mi punto ciego

- `test-psp-titulo.sh --todas` no lo corrí (lo corre el orquestador); `test-psp-arranque`, `-rtc`,
  `-carpeta`, `-vmu`, `-salida-suspension` y `-redondeo` siguen con limitador: no los cambié.
  `test-psp-salida-suspension` llama a `test-psp-igual-pc` con `test_suspend_at`; con el nuevo
  `max_cuadros` y sin limitador no lo corrí.
- Fast-forward depende de que el Tab de `xdotool` llegue a la ventana de PPSSPP en el Xvfb. Si el
  foco falla, la prueba corre con limitador: sale bien pero lenta, sin aviso. No hay una comprobación
  automática de que el limitador se apagó (solo el `wall_s`).
- La igualdad con y sin limitador la comprobé en los cuadros 600 y 2600 con una corrida cada uno; no
  en los cuadros 1243 y 1700 (en 1243 las pruebas dan los mismos PSNR que la fila 9b:
  28.43 / 9.46 / 7.37 dB).
- `dibujar_desde`: comprobé captura idéntica solo en el cuadro 2600. En otros cuadros, una imagen que
  la caché de una corrida completa conservara y que ya no coincide con la VRAM podría dar otra
  imagen; por diseño se decodifica de la VRAM del momento, pero no lo medí.
- Los tiempos «antes» son de otras filas, no de esta rama con limitador (salvo 600 y 2600 del paso
  1). Otros agentes corrían PPSSPP en la misma máquina; esperé a que no hubiera ninguno antes de cada
  prueba, pero no controlé la carga del host.
- El paso 3 (estado guardado) es solo una estimación hecha leyendo las cabeceras; no compilé nada.
