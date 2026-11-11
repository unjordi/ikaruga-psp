# Plan de ikaruga-psp

Una sola lista de pasos, en el orden en que se hacen. El siguiente paso es el primero sin ✅. Cada paso dice qué se hace, cómo (con el diseño que lo sostiene) y cuándo se da por cerrado. Se actualiza en el mismo PR que cierra o abre un paso.

Marcas: ✅ hecho · 🔄 en curso · ⬜ pendiente. Los códigos entre corchetes (A4, B6, C8, T5) son los ítems de `docs/revision-optimizaciones.md`; el detalle de cada medición está en `docs/mediciones/` (hoy todavía en `docs/`).

Meta: Ikaruga corriendo en una PSP-3001 (64 MB, 333 MHz, 480×272), nativo, sin emulador, con el disco que el usuario extrae de su copia. Hoy corre en PPSSPP a ≈5 fps en el capítulo 1; jugable pide ≈30. Meta realista a corto plazo: verlo arrancar en la consola (título, menú, capítulo 1 a pocos fps); jugarlo bien es trabajo de meses.

Lo que no cambia en la PSP: el código traducido del SH-4 (`gen/`) y el núcleo del runtime de dream-recomp (memoria, SH-4, scheduler, Holly, PVR por software, AICA, Maple, GD-ROM lógico, HLE de BIOS) son C++ sin dependencias de host; la decodificación de listas y texturas (`dream::render`) no usa GPU; el runtime no crea hilos; el release corre sin intérprete (`untranslated call targets: 0`). Lo que depende del host son las 13 piezas que se sustituyen en los pasos de abajo.

## Pasos

### Corre y se ve en PPSSPP

1. ✅ Elegir recompilador: dream-recomp (phobos665, GPL-2.0), el único que ya traía Ikaruga configurado y llega al capítulo 1 con texturas en PC. dcrecomp no dibujó ningún cuadro. (`docs/framework-comparacion.md`, PR #2)
2. ✅ Receta reproducible para extraer el disco y recompilar en PC, con el sha256 del C++ generado (determinista). (`docs/reproducir-pc.md`, PR #4)
3. ✅ Toolchain de PSP fijado en Docker (PSPDEV por sha256, Ubuntu por snapshot); EBOOT idéntico byte a byte entre builds limpios. (PR #3)
4. ✅ El C++ generado más el runtime compilan para el Allegrex: a -Os cabe (13,7 MB + 28,1 MB del guest), a -O2 no. (`docs/tamano-psp.md`, PR #6)
5. ✅ Medición en PC con Ikaruga: velocidad en gameplay, perfil (memoria 50 %, AICA 17 %, espera de vblank 58 %), sin doble precisión del SH-4, FTRV/FIPR en las funciones Ninja, cobertura 28 % de funciones en título + capítulo 1. (`docs/medicion-pc.md`, PR #5)
6. ✅ Pieza 13, proceso: 333 MHz y VFPU en el hilo principal (`scePowerSetClockFrequency(333,333,166)`, `PSP_THREAD_ATTR_VFPU`; idea de sf64 `src/psp/main.c:23,78`).
7. ✅ Piezas 8, 10 y 11, archivos: el EBOOT lee `1ST_READ.BIN` y el disco con `sceIo` desde su carpeta (`argv[0]`), sin volcados de depuración ni log del ABI. Arranca en PPSSPP sin video: mismos ciclos y PC que en PC en los cuadros 60–180. (`docs/arranque-psp.md`, PR #9)
8. ✅ Idle-skip: saltar la espera de `kmWaitVBlank` hasta el siguiente evento del scheduler, en PC y en la PSP (≈5× más rápido en PPSSPP, mismo guest). (`docs/idle-skip.md`, PR #10, #12)
9. ✅ Piezas 1, 2 y 4, video mínimo: `sceGuInit`, listas opaca/punch-through/translúcida con `sceGuDrawArray`, dibujo directo a 480×272 con la escala en los vértices, doble búfer. Misma geometría que PC (947 polígonos en el cuadro 2600). (`docs/geometria-psp.md`, PR #19; ideas de sf64 `renderer.c:1156-1194,2001` y oot `gfx_scegu.c:367,1802`)
10. ✅ Pieza 3, texturas: decodificación de twiddled, VQ y paletas; pantalla de título texturizada y legible. (`docs/titulo-psp.md`, PR #21)
11. ✅ Correctitud: redondeo del guest escrito en el FCSR (newlib no lo hacía), RTC con semilla, HOME, reapertura del disco tras suspender, carpeta desde `argv[0]`, VMU que se guarda (pieza 9: temporal + reemplazo, idea de sf64 `settings.c:75-80,131-135`); la PSP escribe lo mismo que PC escritura por escritura en los cuadros 60–600. (`docs/correctitud-psp.md`, PR #16)
12. ✅ Disco real: `DISCO.ISO` volcado del área HD del CHD (`tools/volcar-area-hd.cpp`); el ISO armado con `genisoimage` diverge desde el cuadro 4.
13. ✅ Velocidad: runtime a -O2 con el generado a -Os [A1]; caché de texturas por páginas sucias con presupuesto de 2,5 MB; FIPR/FTRV/FMAC en `float` en PC y PSP, con `double` como opción de compilación [A3]. 0,0405x → 0,0869x en el capítulo 1. (`docs/velocidad-psp.md`, PR #18, #23, #27)
14. ✅ Costados del HUD (recorte de usuario del PVR) y niebla de tabla. Con la niebla el capítulo 1 queda en 0,0830x (≈5 fps): la segunda pasada cuesta ≈4,5 %. (`docs/video-psp.md`, PR #24)
15. ✅ Receta de primer uso de punta a punta desde un directorio limpio. (`docs/primer-uso.md`, PR #22)
16. ✅ `.gitignore` cubre el disco, las capturas, la tarjeta de memoria, el cfg y el log en cualquier ruta, con `scripts/test-gitignore.sh`. (PR #29)
17. ✅ Herramientas de referencia contra Flycast: capturar el mismo disco en Flycast y puntuar escenas de la PSP contra él (PSNR/SSIM). (`docs/referencia-flycast.md`, PR #30)

### Pruebas rápidas y lo que falta para poder jugar

18. ✅ Integrar el arreglo de denormales del build `double` (la PSP no aplanaba denormales al leer; con `double` la PSP vuelve a ser igual a PC hasta el cuadro 2600 con pulsaciones). Batería verde sobre develop y `double` igual al PC en cada cuadro 1–2600. (PR #35)
19. ✅ Pieza 5, controles [T5]: `sceCtrlPeekBufferPositive` cada cuadro (modo analógico, `sceCtrlSetSamplingCycle(0)`) → mando Maple, combinado con las pulsaciones guionadas; mapeo configurable en `ikaruga.cfg`, stick → cruceta con umbral, mapeo alterno para TATE (idea de sf64 `src/psp/input.c:75-86`). Verificado en PPSSPP: START real lleva al título y con `control=0` no; Cruz remapeada a START funciona; en el menú de modos la cruceta real, el stick y la izquierda con `tate=1` bajan el cursor y la izquierda sin TATE no; el arranque sin pulsaciones sigue igual al PC. Sin verificar: R y A+B soltando la energía en juego, `tate=2`, el umbral del stick. (`docs/controles-psp.md`)
20. ✅ PPSSPP sin limitador en las pruebas de imagen y correctitud (la velocidad se sigue midiendo con limitador). Misma captura y mismos hashes con y sin limitador; el cuadro 2600 pasa de 531 s a 37 s de pared. Grabar para Flycast y medir velocidad siguen con limitador. (`docs/pruebas-rapidas.md`, PR #36)
21. ✅ `dibujar_desde=N` en el cfg: no dibujar hasta el cuadro de captura. Captura idéntica; ahorra poco (≈4 s de 37) porque sin limitador el tiempo ya lo pone la CPU del guest. (PR #36)
22. ✅ Estado guardado: investigar si el runtime ya serializa el estado del guest. dream-recomp no lo trae; hacen falta `save/load` en ≈12 piezas del runtime (≈600–900 líneas) y un punto de reanudación seguro. (`docs/pruebas-rapidas.md` §3)
23. ⬜ Estado guardado, punto de volcado: en `Hooks::on_idle` con el guest en la cabeza de la espera de `kmWaitVBlank` (`0x0C0370BA`, ya tiene etiqueta de reanudación y `idle_poll`) al cruzar el cuadro N; vuelca `Ctx`, RAM, VRAM, ARAM y el scheduler.
24. ⬜ Estado guardado, arranque reanudado: `run_guest_from(c, m, pc)` en `abi_bare.cpp` (parche al submódulo) que entra a `run_guest_impl` ya reanudado, y carga del volcado en `main.cpp` (`cargar_estado=` en el cfg).
25. ⬜ Estado guardado, dispositivos: `save/load` de PVR/TA, AICA/ARM7, Maple, BIOS HLE y épocas de VRAM (`docs/pruebas-rapidas.md` §3). Cierra con `write_hash` idéntico en los 60 cuadros siguientes al volcado, en el build `double`.
26. ⬜ Una pulsación guionada dura 8 cuadros y el menú de modos la ignora en el cuadro 1660 (la tecla real, sostenida más, sí mueve el cursor): hacer configurable la duración o sostenerla hasta que el juego la lea, para que las pruebas guionadas no fallen en silencio.
27. ⬜ `probar-ikaruga-ppsspp.sh` falla si falta `PROBAR_DISCO` en vez de armar el ISO que diverge.
28. ⬜ `scripts/bateria.sh` versionado: corre todas las pruebas en orden, reusa las capturas repetidas (la (c) de video es la (a) de título) y deja un resumen.
29. ⬜ El build construye solo la variante mix por defecto (hoy hace mix y -Os y duplica el tiempo).
30. ⬜ Los comparadores encuentran solos el socket de Wayland de la máquina en vez de suponer `wayland-1`.
31. ⬜ Idle-skip barato [A8]: tabla chica en vez de `std::map`, sin copiar ni comparar el contexto entero de 296 B en cada sondeo, división de 32 bits. Cierra con mismo guest y menos tiempo en PPSSPP.
32. ⬜ Camino rápido a la RAM del guest [A4]: `read/write` no virtuales que van directo a `ram + (a & 0x00FFFFFF)` en el área 3 (excluyendo P4, `a >= 0xE0000000`) y caen al camino lento para lo demás; `store` sin journaling en el camino rápido. Era "obligatorio antes de medir en serio" en el diseño (el estudio de dream-recomp midió 3,5× → 5,8× con lo mismo). Cierra con mismo hash en el build `double` y velocidad medida antes/después.
33. ⬜ Medir la memoria real del EBOOT actual: pico de heap en título y capítulo 1, contra el presupuesto. Presupuesto del diseño para ≈52 MB útiles: guest 28,1 MB (RAM 16 + VRAM 8 + ARAM 2 + Boot ROM 2 + flash/OCRAM 0,13), código 13,8 MB (mix), data+bss ≈0,6 MB, caché de texturas 2,5 MB, decodificación temporal hasta 2 MB, listas GE, pilas y newlib 1–2 MB; los framebuffers van en la eDRAM. Si falta memoria, en este orden: quitar la Boot ROM (2 MB; Ikaruga corre sin BIOS), código frío a -Os o a overlay, ARAM solo si el audio es HLE; RAM y VRAM del guest no se tocan.
34. ⬜ Limpieza del código: cabeceras desactualizadas (`main.cpp` "headless"/`kLog`, `host.h` "video stub", `video.cpp` "fog not drawn", `disc.cpp` que manda a `preparar-disco`), comentarios "Row N:", el mensaje de error del disco faltante (debe mandar al volcado del área HD), título del EBOOT "headless", claves del cfg en un solo idioma y un solo lector de `ikaruga.cfg` (hoy lo leen `main.cpp` e `input.cpp` por separado), `build-out.txt` fuera del repo.
35. ⬜ Limpieza de los docs: `docs/README.md` como índice; los vigentes (`plan`, `primer-uso`, `reproducir-pc`, `referencia-flycast`, `controles-psp`) al día; las fotos de medición de cada fila a `docs/mediciones/` con un aviso de que son foto; `BITACORA-NOCHE.md` fuera; README del repo y `scripts/README` con el estado real; `reproducir-pc` sin citar archivos que solo existen en una máquina.
36. ⬜ `scripts/armar-docs.sh`: junta el plan y los docs vigentes en un solo PDF con índice (pandoc), para leerlo de corrido.

### Se ve como el original (referencia: Flycast)

37. ⬜ Las comparaciones contra PC alinean el instante y la consola: el PC se lanza con una tarjeta en blanco (`--vmu`), como la PSP, y se toma el cuadro presentado que corresponde al cuadro del guest de la PSP (hoy `--screenshot-at N` cuenta cuadros presentados, 1–2 detrás). Medido: el centro del capítulo 1 sube de 15,7 a 19,0 dB sin tocar el port. (`docs/investigacion-pendientes.md` §36, §37)
38. ⬜ La batería de imagen (`comparar-titulo` y las pruebas que lo usan) compara contra Flycast por defecto; el PC queda como opción para depurar. Cierra cuando título, menú y capítulo 1 tienen su puntuación contra Flycast y un umbral fijado antes de medir.
39. ⬜ Comparar develop contra Flycast escena por escena: logo, título, menú de modos, pausa, inicio del capítulo 1, un punto del capítulo 1. Cada diferencia visible se vuelve un paso nuevo justo debajo de este.
40. ⬜ Centro del capítulo 1, medir: con `trazar=1` y `volcar_ta=1` en el cuadro 2600, dos contrapruebas en `video_draw_frame`: sin las tiras con vértices fuera de ±2048 px (172 tiras: suelo y nubes) y con `GU_NEAREST`. Dice cuál de las dos causas explica el resto del error.
41. ⬜ Polígonos fuera del rango del GE en modo through: recortarlos contra la banda de guarda en `video_draw_frame` (o pasar esas tiras a modo 3D con el clipper del GE). Cierra con el centro del capítulo 1 contra Flycast.
42. ⬜ Mipmaps en el GE para las texturas que los traen (473 opacas por render; `texture_psp.cpp` sube los niveles, `sceGuTexLevelMode`), solo si la contraprueba de filtrado lo señala.
43. ⬜ Profundidad: repartir los 16 bits sin los planos de HUD y fondo (1/w ≥ 1) o en log2(1/w) como el PC; hoy miles de profundidades de la escena caen en el mismo escalón (77 escalones para 5253 valores en el cuadro 2000). Cierra con el cuadro 2000 sin escalones compartidos entre polígonos que se tapan (`tools/analizar-ta.cpp`) y la captura contra Flycast.
44. ⬜ Perspectiva de texturas: hay tiras con 1/w ×3 de un extremo a otro (≈280 por render). Mirar el suelo y las estructuras inclinadas del cuadro 2600 contra Flycast; si se ve la deformación afín, subdividir en `video_draw_frame` las tiras con relación > 1,5.
45. ⬜ Color offset (especular): una pasada aditiva sin textura con el color offset, como la de niebla, solo para las tiras con offset distinto de 0 (68 polígonos en el cuadro 2600, 733 en el 4500). Cierra con la escena 4500 contra Flycast.
46. ⬜ Texturas: comprobar que los 2 polígonos PAL8 se dibujan (`trazar=1` en el 2600) y que las pruebas de imagen lean `failed`/`over_budget` del log.
47. ⬜ Referencia de la versión de Steam: capturas locales del título, el menú, el inicio del capítulo 1 y el modo vertical, y en `docs/` lo que esas escenas exigen al port (capas, efectos, orientación), sin imágenes. Medido: el área de juego es 3:4 (810×1080) con paneles a los lados. El área 3:4 con paneles es de la versión de Steam; en Dreamcast el modo vertical llena la pantalla.
48. ⬜ Pantalla en TATE en la PSP: el juego ya gira la imagen y llena los 640×480 cuando se elige `MONITOR TYPE = VERTICAL 1/2` en CONFIG (según la investigación, §50). En la PSP basta con escalar los 640×480 girados a 272×363 en `video_draw_frame` (intercambiar x/y al pasar los vértices) y usar el mapeo de controles con `tate=`; el horizontal queda como opción. Mirar el sentido de VERTICAL 1 frente a 2 y el aspecto. Cierra con capturas de los dos modos.

### Velocidad (meta ≈30 fps)

49. ⬜ Microbenchmark de costos unitarios [T3] en un EBOOT aparte: `fipr`, `ftrv`, `fmaf`, `read32`, `sq_flush`, un `throw` a través de 3 marcos.
50. ⬜ El build de entrega deja de pagar lo que no necesita: hash de escrituras, contadores de medición y log a la Memory Stick pasan a opcionales; el log se escribe en bloque en vez de abrir y cerrar el archivo por línea [C10].
51. ⬜ AICA sin mezclador ni DSP mientras no haya audio [A7]: interruptor de compilación en `Aica::sample_tick` (`aica.cpp`). Medido en PC: 2,9 M instrucciones ARM por segundo de juego y el mezclador se emula siempre (≈17 % del host). Prueba: velocidad en PPSSPP antes/después; contraprueba: `write_hash` del build `double` igual.
52. ⬜ Idle-skip del ARM: el driver de sonido gira en `0x1d10–0x1d48` esperando el FIQ del timer; saltar al siguiente evento del timer en `Arm7::run`. Medir antes qué parte de las 2,9 M instrucciones por segundo cae en ese bucle.
53. ⬜ SPG con menos eventos sin cambiar el comportamiento [A6a]: solo las líneas donde pasa algo; el contador de línea se calcula al leerlo.
54. ⬜ Store queues hacia el TA [A5]: recordar la última traducción de la UTLB, ir directo a la FIFO del TA, sin avance del scheduler por cada ráfaga.
55. ⬜ El GE dibuja en paralelo con la CPU [B1]: dos listas, sincronizar solo antes de reusar la anterior.
56. ⬜ Estado del GE solo cuando cambia [B3].
57. ⬜ Decodificar el TA una sola vez [B5].
58. ⬜ Vértices de 16 bits [B7]. Ojo: la profundidad ya va justa en 16 bits y hay vértices fuera de ±2048 px.
59. ⬜ Framebuffers de 16 bits con dither [B2]: libera ≈0,5 MB de eDRAM y la mitad del ancho de banda de relleno.
60. ⬜ Texturas a 16 bits (4444/5551/5650), CLUT del GE para las de paleta y swizzle para el GE [B6] (idea de oot `psp_texture_manager.c:221,335`).
61. ⬜ Abaratar la segunda pasada de niebla (≈4,5 % del capítulo 1).
62. ⬜ Ritmo (pieza 7): esperar vblank al presentar y no pasar de 60 Hz (`sceDisplayWaitVblankStart`, ritmo del Dreamcast NTSC; `clock_wait_vblank` ya existe sin uso). Si la lógica no alcanza 60 Hz: saltar dibujo de cuadros [B8] antes que ralentizar.
63. ⬜ Higiene del build [A14]: `-ffunction-sections -fdata-sections -Wl,--gc-sections`, `-fno-math-errno`; comprobar `untranslated call targets: 0`.
64. ⬜ -O2 solo en las funciones calientes del generado [A10], con la cobertura de los capítulos 1–5.
65. ⬜ Contador de ciclos de 32 bits en el código emitido [A11].
66. ⬜ Quitar las ramas `FPSCR.PR` del código emitido [A12]: PR=0 salvo las funciones de doble de la libm; `write_fpscr` falla en voz alta si aparece PR=1 fuera de ellas.
67. ⬜ `[idle]` para la espera del GD-ROM (`gdFsWaitCompleteReq`, cabeza `0x0C040E14`): solo afecta a las cargas (≈3,7 % del guest, arranque y entrada al capítulo); saltar hasta el siguiente evento `gdrom-hle`.

### Juego completo

68. ⬜ Pieza 6, audio: hilo con `sceAudioSRCChReserve` + `sceAudioSRCOutputBlocking` y la misma política de cola y descarte del PC (idea de sf64 `audio_output.c:8-12,300,307`: 32 kHz, 8 bloques). Revisar el heap que queda fuera para la pila del hilo [C9]. Orden de opciones: primero el AICA en el núcleo principal para validar; luego al Media Engine [B9] (≈3 MIPS de ARM a 30–60 ciclos por instrucción = 90–180 MHz del ME, memoria compartida sin caché en `0x40000000`, como sf64 `audio_me.c`; PPSSPP no ejecuta el ME); si no alcanza, traducir estáticamente el driver de sonido (requiere comprobar que no se modifica en ARAM); HLE solo si la memoria obliga. Comprobado en título y capítulo 1: el código del driver del ARM no cambia en ARAM (solo sus variables), condición para traducirlo estáticamente.
69. ⬜ Recorrer los capítulos 2 a 5 y los jefes en PPSSPP con controles: anotar todo lo que se vea mal como paso nuevo de la sección de Flycast. En cada uno: `texture_renders` sigue en 0 (si no, abrir el render a textura), repetir la comparación de ARAM del driver de sonido y muestrear con `--sample` la pausa y el cambio de capítulo por si hay otra espera para `[idle]`.
70. ⬜ Tarjeta de memoria: al arrancar, si falta `VMU.BIN` y existe `VMU.BIN.TMP`, usarla (hueco de un corte de energía).
71. ⬜ `DISCO.ISO` desde el CHD del usuario con un solo comando documentado, sin compilar a mano; retirar `preparar-disco-ikaruga.sh`. Cierra cuando `test-psp-igual-pc.sh` pasa con ese disco.
72. ⬜ Empaquetado: título del EBOOT, ícono, `MEMSIZE=1` (ya), e instrucciones para poner el disco propio en `ms0:`.
73. ⬜ Texturas HD de la versión de Steam (`tex.arc`, NARC): identificar el formato, emparejar con las del disco, reducirlas a lo que quepa en la PSP (fuentes, HUD, menús, fondos). Se extraen en la máquina del usuario, nunca en el repo.

### En la PSP real

74. ⬜ Instrucciones para custom firmware (ARK/PRO) y copiar `EBOOT.PBP`, `1ST_READ.BIN` y `DISCO.ISO` (~1 GB) a la Memory Stick.
75. ⬜ Primer arranque en la PSP-3001: llega al título; presentación en pantalla (hasta ahora solo capturas).
76. ⬜ Sonda de redondeo en hardware: que el Allegrex respete RM y FS del FCSR [C13].
77. ⬜ HOME y suspensión reales.
78. ⬜ Lectura del disco por bloques en vez de sector por sector [C8]; costo de escribir la VMU (128 KB por bloque) en la Memory Stick.
79. ⬜ Riesgos que solo aparecen en hardware: coordenadas negativas en modo through, alfa y stencil, vblank, coherencia de la caché de datos. También la pasada de niebla con profundidad EQUAL y los vértices fuera de ±2048 px.
80. ⬜ Velocidad medida en la consola por escena.
81. ⬜ Orden del código caliente para la caché de instrucciones de 16 KB [B10] y scratchpad de 16 KB [B11], medidos en hardware.

### Red de pruebas completa

82. ⬜ Pruebas por caso de uso de todo lo construido (imagen contra Flycast por escena, correctitud del build `double`, velocidad, controles, audio), con su contraprueba; reusar y ordenar las existentes. Retirar `test-psp-geometria` y `comparar-geometria` (la IoU pasa casi por construcción); `comparar-titulo` → `comparar-escena`; `test-psp-texturas-juego` (b) con referencia fija para que pueda fallar. Cubrir el parpadeo de PRESS START.
