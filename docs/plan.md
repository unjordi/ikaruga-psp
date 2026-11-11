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
23. ⬜ Estado guardado: si el paso 22 dice que es factible, volcar el estado en un cuadro y arrancar desde ahí. Cierra cuando el hash de los cuadros siguientes es idéntico al de una corrida desde el arranque.
24. ⬜ Una pulsación guionada dura 8 cuadros y el menú de modos la ignora en el cuadro 1660 (la tecla real, sostenida más, sí mueve el cursor): hacer configurable la duración o sostenerla hasta que el juego la lea, para que las pruebas guionadas no fallen en silencio.
25. ⬜ `probar-ikaruga-ppsspp.sh` falla si falta `PROBAR_DISCO` en vez de armar el ISO que diverge.
26. ⬜ `scripts/bateria.sh` versionado: corre todas las pruebas en orden, reusa las capturas repetidas (la (c) de video es la (a) de título) y deja un resumen.
27. ⬜ El build construye solo la variante mix por defecto (hoy hace mix y -Os y duplica el tiempo).
28. ⬜ Los comparadores encuentran solos el socket de Wayland de la máquina en vez de suponer `wayland-1`.
29. ⬜ Idle-skip barato [A8]: tabla chica en vez de `std::map`, sin copiar ni comparar el contexto entero de 296 B en cada sondeo, división de 32 bits. Cierra con mismo guest y menos tiempo en PPSSPP.
30. ⬜ Camino rápido a la RAM del guest [A4]: `read/write` no virtuales que van directo a `ram + (a & 0x00FFFFFF)` en el área 3 (excluyendo P4, `a >= 0xE0000000`) y caen al camino lento para lo demás; `store` sin journaling en el camino rápido. Era "obligatorio antes de medir en serio" en el diseño (el estudio de dream-recomp midió 3,5× → 5,8× con lo mismo). Cierra con mismo hash en el build `double` y velocidad medida antes/después.
31. ⬜ Medir la memoria real del EBOOT actual: pico de heap en título y capítulo 1, contra el presupuesto. Presupuesto del diseño para ≈52 MB útiles: guest 28,1 MB (RAM 16 + VRAM 8 + ARAM 2 + Boot ROM 2 + flash/OCRAM 0,13), código 13,8 MB (mix), data+bss ≈0,6 MB, caché de texturas 2,5 MB, decodificación temporal hasta 2 MB, listas GE, pilas y newlib 1–2 MB; los framebuffers van en la eDRAM. Si falta memoria, en este orden: quitar la Boot ROM (2 MB; Ikaruga corre sin BIOS), código frío a -Os o a overlay, ARAM solo si el audio es HLE; RAM y VRAM del guest no se tocan.
32. ⬜ Limpieza del código: cabeceras desactualizadas (`main.cpp` "headless"/`kLog`, `host.h` "video stub", `video.cpp` "fog not drawn", `disc.cpp` que manda a `preparar-disco`), comentarios "Row N:", el mensaje de error del disco faltante (debe mandar al volcado del área HD), título del EBOOT "headless", claves del cfg en un solo idioma y un solo lector de `ikaruga.cfg` (hoy lo leen `main.cpp` e `input.cpp` por separado), `build-out.txt` fuera del repo.
33. ⬜ Limpieza de los docs: `docs/README.md` como índice; los vigentes (`plan`, `primer-uso`, `reproducir-pc`, `referencia-flycast`, `controles-psp`) al día; las fotos de medición de cada fila a `docs/mediciones/` con un aviso de que son foto; `BITACORA-NOCHE.md` fuera; README del repo y `scripts/README` con el estado real; `reproducir-pc` sin citar archivos que solo existen en una máquina.
34. ⬜ `scripts/armar-docs.sh`: junta el plan y los docs vigentes en un solo PDF con índice (pandoc), para leerlo de corrido.

### Se ve como el original (referencia: Flycast)

35. ⬜ La batería de imagen (`comparar-titulo` y las pruebas que lo usan) compara contra Flycast por defecto; el PC queda como opción para depurar. Cierra cuando título, menú y capítulo 1 tienen su puntuación contra Flycast y un umbral fijado antes de medir.
36. ⬜ Comparar develop contra Flycast escena por escena: logo, título, menú de modos, pausa, inicio del capítulo 1, un punto del capítulo 1. Cada diferencia visible se vuelve un paso nuevo justo debajo de este.
37. ⬜ El centro del capítulo 1 (≈16 dB contra el PC, causa sin averiguar): medir contra Flycast y aislar la causa (color offset, modifier volumes, filtrado u otra).
38. ⬜ Un render de menos: al cuadro 2600 la PSP lleva 2319 renders y PC 2320; averiguar de dónde sale antes de comparar escenas largas.
39. ⬜ Render a textura del PVR (`FB_W_SOF1` bit 24): medir si el juego lo usa en los capítulos 1–5. Hoy se cuenta y se dibuja a pantalla.
40. ⬜ Si el paso 39 dice que sí: dibujar a textura en la VRAM del guest y no presentarlo.
41. ⬜ Profundidad: la PSP usa 1/w lineal en 16 bits (PC usa log2); medir z-fighting en escenas con superficies juntas y corregir si aparece.
42. ⬜ Perspectiva de texturas en modo through: medir si se nota la deformación afín.
43. ⬜ Culling: hoy siempre apagado; medir si el juego depende de él.
44. ⬜ Modifier volumes (sombras): medir si el juego los usa y dibujarlos.
45. ⬜ Color offset (especular) y color clamp: dibujarlos.
46. ⬜ Niebla: translúcidas con factor de origen distinto de alfa/1 (hoy sin niebla), modos de niebla 1 y 3, y la segunda pasada con profundidad EQUAL (riesgo en hardware).
47. ⬜ Texturas: llave de caché con `PAL_RAM_CTRL` y el ancho de stride; formatos sin ejercitar (espejo, YUV, bump, PAL8, paleta 8888); decal alpha y modulate con ignore_texture_alpha comparados polígono a polígono; las texturas que no caben salen sin textura en silencio (contarlas en el log).
48. ⬜ Denormales en el build `float` (el SH-4 los aplana al leer y la FPU de la PSP no): corregir solo si Flycast muestra diferencia.
49. ⬜ Parpadeo de PRESS START y recorte de usuario modo 3: cubrirlos con una escena.
50. ⬜ Referencia de la versión de Steam: capturas locales del título, el menú, el inicio del capítulo 1 y el modo vertical, y en `docs/` lo que esas escenas exigen al port (capas, efectos, orientación), sin imágenes. Medido: el área de juego es 3:4 (810×1080) con paneles a los lados.
51. ⬜ Modo vertical (TATE) en el recompilado de PC: confirmar cómo lo dibuja el juego (si rota y llena los 640×480). En horizontal el título es una columna ≈360×480 al centro.
52. ⬜ Pantalla en TATE en la PSP: girar la proyección del GE (sin pasada extra) y usar el mapeo de controles TATE del paso 19; el horizontal queda como opción del cfg. Modos: horizontal deja 204×272 para el área de juego (55 488 px, 276 columnas sin usar); TATE, con la consola de lado, 272×363 (98 736 px, 1,78× más). Cierra con capturas de los dos modos.

### Velocidad (meta ≈30 fps)

53. ⬜ Microbenchmark de costos unitarios [T3] en un EBOOT aparte: `fipr`, `ftrv`, `fmaf`, `read32`, `sq_flush`, un `throw` a través de 3 marcos.
54. ⬜ El build de entrega deja de pagar lo que no necesita: hash de escrituras, contadores de medición y log a la Memory Stick pasan a opcionales; el log se escribe en bloque en vez de abrir y cerrar el archivo por línea [C10].
55. ⬜ Medir cuánto cuesta el AICA en la PSP (se emula entero y su salida se tira).
56. ⬜ AICA reducido mientras no haya audio [A7]: DSP apagado y canales que avanzan sin mezclar.
57. ⬜ Otros bucles de espera para `[idle]`: perfilar dónde gira el guest en carga, GD-ROM, pausa y cambio de capítulo, y declararlos.
58. ⬜ SPG con menos eventos sin cambiar el comportamiento [A6a]: solo las líneas donde pasa algo; el contador de línea se calcula al leerlo.
59. ⬜ Store queues hacia el TA [A5]: recordar la última traducción de la UTLB, ir directo a la FIFO del TA, sin avance del scheduler por cada ráfaga.
60. ⬜ El GE dibuja en paralelo con la CPU [B1]: dos listas, sincronizar solo antes de reusar la anterior.
61. ⬜ Estado del GE solo cuando cambia [B3].
62. ⬜ Decodificar el TA una sola vez [B5].
63. ⬜ Vértices de 16 bits [B7].
64. ⬜ Framebuffers de 16 bits con dither [B2]: libera ≈0,5 MB de eDRAM y la mitad del ancho de banda de relleno.
65. ⬜ Texturas a 16 bits (4444/5551/5650), CLUT del GE para las de paleta y swizzle para el GE [B6] (idea de oot `psp_texture_manager.c:221,335`).
66. ⬜ Abaratar la segunda pasada de niebla (≈4,5 % del capítulo 1).
67. ⬜ Ritmo (pieza 7): esperar vblank al presentar y no pasar de 60 Hz (`sceDisplayWaitVblankStart`, ritmo del Dreamcast NTSC; `clock_wait_vblank` ya existe sin uso). Si la lógica no alcanza 60 Hz: saltar dibujo de cuadros [B8] antes que ralentizar.
68. ⬜ Higiene del build [A14]: `-ffunction-sections -fdata-sections -Wl,--gc-sections`, `-fno-math-errno`; comprobar `untranslated call targets: 0`.
69. ⬜ -O2 solo en las funciones calientes del generado [A10], con la cobertura de los capítulos 1–5.
70. ⬜ Contador de ciclos de 32 bits en el código emitido [A11].
71. ⬜ Quitar las ramas `FPSCR.PR` del código emitido [A12]: PR=0 salvo las funciones de doble de la libm; `write_fpscr` falla en voz alta si aparece PR=1 fuera de ellas.

### Juego completo

72. ⬜ Pieza 6, audio: hilo con `sceAudioSRCChReserve` + `sceAudioSRCOutputBlocking` y la misma política de cola y descarte del PC (idea de sf64 `audio_output.c:8-12,300,307`: 32 kHz, 8 bloques). Revisar el heap que queda fuera para la pila del hilo [C9]. Orden de opciones: primero el AICA en el núcleo principal para validar; luego al Media Engine [B9] (≈3 MIPS de ARM a 30–60 ciclos por instrucción = 90–180 MHz del ME, memoria compartida sin caché en `0x40000000`, como sf64 `audio_me.c`; PPSSPP no ejecuta el ME); si no alcanza, traducir estáticamente el driver de sonido (requiere comprobar que no se modifica en ARAM); HLE solo si la memoria obliga.
73. ⬜ Recorrer los capítulos 2 a 5 y los jefes en PPSSPP con controles: anotar todo lo que se vea mal como paso nuevo de la sección de Flycast.
74. ⬜ Tarjeta de memoria: al arrancar, si falta `VMU.BIN` y existe `VMU.BIN.TMP`, usarla (hueco de un corte de energía).
75. ⬜ `DISCO.ISO` desde el CHD del usuario con un solo comando documentado, sin compilar a mano; retirar `preparar-disco-ikaruga.sh`. Cierra cuando `test-psp-igual-pc.sh` pasa con ese disco.
76. ⬜ Empaquetado: título del EBOOT, ícono, `MEMSIZE=1` (ya), e instrucciones para poner el disco propio en `ms0:`.
77. ⬜ Texturas HD de la versión de Steam (`tex.arc`, NARC): identificar el formato, emparejar con las del disco, reducirlas a lo que quepa en la PSP (fuentes, HUD, menús, fondos). Se extraen en la máquina del usuario, nunca en el repo.

### En la PSP real

78. ⬜ Instrucciones para custom firmware (ARK/PRO) y copiar `EBOOT.PBP`, `1ST_READ.BIN` y `DISCO.ISO` (~1 GB) a la Memory Stick.
79. ⬜ Primer arranque en la PSP-3001: llega al título; presentación en pantalla (hasta ahora solo capturas).
80. ⬜ Sonda de redondeo en hardware: que el Allegrex respete RM y FS del FCSR [C13].
81. ⬜ HOME y suspensión reales.
82. ⬜ Lectura del disco por bloques en vez de sector por sector [C8]; costo de escribir la VMU (128 KB por bloque) en la Memory Stick.
83. ⬜ Riesgos que solo aparecen en hardware: coordenadas negativas en modo through, alfa y stencil, vblank, coherencia de la caché de datos.
84. ⬜ Velocidad medida en la consola por escena.
85. ⬜ Orden del código caliente para la caché de instrucciones de 16 KB [B10] y scratchpad de 16 KB [B11], medidos en hardware.

### Red de pruebas completa

86. ⬜ Pruebas por caso de uso de todo lo construido (imagen contra Flycast por escena, correctitud del build `double`, velocidad, controles, audio), con su contraprueba; reusar y ordenar las existentes. Retirar `test-psp-geometria` y `comparar-geometria` (la IoU pasa casi por construcción); `comparar-titulo` → `comparar-escena`; `test-psp-texturas-juego` (b) con referencia fija para que pueda fallar.
