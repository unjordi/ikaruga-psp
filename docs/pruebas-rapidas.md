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

(se completa abajo)
