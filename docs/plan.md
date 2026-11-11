# Plan de ikaruga-psp

Fuente única del orden del trabajo. Se actualiza en el mismo PR que cierra o abre cada fila. Las letras y números (A4, B6, C8, T5) son los ítems de `docs/revision-optimizaciones.md`; las filas numeradas tienen su detalle y su criterio de cierre más abajo.

## Fases

### Fase 1 · Corre y se ve (PPSSPP) — ✅ hecha
- ✅ 0–4b recompilador, diseño, toolchain, compila, medición · 5a–5d arranca, idle-skip (el mecanismo; la versión barata A8 sigue abierta, Fase 1.5), geometría · 6 título con texturas · 6b costados HUD · 6c niebla (el render a textura NO se hizo: se cuenta y se dibuja a pantalla → reabierto en Fase 2) · 8 correctitud (A2, T1, T2) · e2e receta de primer uso.
- Fila 9 (velocidad): su meta 0,105x no se cumplió; quedó en 0,0830x en develop con #24 (la segunda pasada de niebla cuesta ≈4,5 %). Sigue en la Fase 3.
- Auditoría archivo por archivo (2026-11-11): todos sus hallazgos están repartidos abajo.

### Fase 1.5 · Pruebas rápidas (acelera todo lo que sigue; va antes de medir mucho contra Flycast)
- ⬜ PPSSPP sin limitador de cuadros para las pruebas de imagen y correctitud (hoy wall_s ≈ host_s: parece correr a ritmo de PSP [sin medir]); la velocidad se sigue midiendo con limitador.
- ⬜ No dibujar hasta el cuadro de captura (cfg `dibujar_desde=N`): el dibujo cuesta 7–33 ms por render y casi ningún cuadro se mira.
- ⬜ Arrancar desde un estado guardado (volcar el estado del guest en un cuadro y cargarlo en el EBOOT) para correr solo los cuadros que se prueban.
- ⬜ A4 camino rápido a la RAM del guest: acelera el juego y todas las pruebas que queden (se adelanta desde la fase 3).
- ⬜ A8 idle-skip barato: tabla chica en vez de `std::map`, sin copiar ni comparar el `Ctx` entero en cada sondeo, división de 32 bits.
- 🔄 T5 controles (en curso: `psp/stubs/input.cpp` + `docs/controles-psp.md`): leer los botones de la PSP (`sceCtrlPeekBufferPositive`, modo digital) y pasarlos al Maple del guest; sin esto nadie juega. Antes que el audio, como decía el diseño.
- ⬜ Higiene que protege y acelera (lo de `probar-ikaruga-ppsspp.sh` va después de la entrega en curso de esta fase, que toca el mismo archivo): (✅ `.gitignore` + `scripts/test-gitignore.sh`, PR #29 `2581427`) `scripts/bateria.sh` versionado (hoy vive en /tmp de la cachy) que reusa capturas repetidas; `probar-ikaruga-ppsspp.sh` falla sin `PROBAR_DISCO` en vez de usar el ISO que diverge; build solo mix por defecto; comparadores que encuentran el socket de Wayland en vez de suponer `wayland-1`.
- ⬜ Medir la memoria real del EBOOT actual (pico de heap en título y capítulo 1): el "cabe" de hoy es la estimación de la fila 4.
- ⬜ Limpieza de comentarios y docs: cabeceras desactualizadas (`main.cpp` "headless"/`kLog`, `host.h`, `video.cpp` "fog not drawn", `disc.cpp`), comentarios "Row N:" en el código, mensaje de error que manda a `preparar-disco`, título del EBOOT, `build-out.txt`, README, `scripts/README`. Docs: `docs/README.md` como índice; los vigentes (`plan`, `diseno-psp`, `primer-uso`, `reproducir-pc`, `referencia-flycast`, `controles-psp`) se mantienen al día; las fotos de medición de cada fila pasan a `docs/mediciones/` con un aviso de que son foto y que el estado vigente es este plan; `BITACORA-NOCHE.md` sale del repo.

### Fase 2 · Se ve como el original (oráculo: Flycast)
- 🔄 8b denormales en build double (#28) → integrar · ✅ E herramientas Flycast (PR #30 `6f2403f`, rebase de #26).
- ⬜ (primero de la fase; se lanza en cuanto entregue la Fase 1.5, que toca los mismos scripts de prueba) Pasar la batería de imagen (comparar-titulo y sus pruebas) a Flycast como referencia por defecto; el PC queda como opción para depurar.
- ⬜ E-medición: comparar develop integrado contra Flycast escena por escena (título, menús, pausa, inicio y punto del capítulo 1); cada diferencia → ítem de esta fase. Primero el centro del capítulo 1 (≈16 dB contra PC, causa sin averiguar).
- ⬜ 6c render a textura: medir si el juego lo usa en los capítulos 1–5 y, si lo usa, dibujarlo a textura.
- ⬜ Un render de menos: al cuadro 2600 la PSP lleva 2319 renders y PC 2320 (misma corriente del TA en el render capturado); averiguar de dónde sale antes de comparar escenas largas.
- ⬜ 6d sospechas de video, cada una medida antes de arreglar: profundidad (z-fighting), perspectiva de texturas, culling, modifier volumes (sombras), DAZ en float, stride.
- ⬜ R TATE (pantalla vertical) con su CIERRA de la fila R: capturas del modo vertical y el doc de lo que el port necesita (rotación, escala, remapeo de controles).

### Fase 3 · Velocidad (meta jugable ≈30 fps; hoy ≈5 fps en el capítulo 1)
- ✅ A1 runtime -O2 · A9 contadores · A3 FPU float · caché de texturas · A13 medido (no vale la pena).
- (A4 adelantado a la fase 1.5)
- ⬜ A6a SPG con menos eventos, sin cambiar el comportamiento (el intento anterior lo cambiaba) · A5 SQ/TA (intento +1,9 %, revertido).
- ⬜ Video en paralelo y más barato: B1 GE en paralelo con la CPU · B5 TA decodificado una vez · B3 estado solo cuando cambia · B7 vértices de 16 bits · B2 framebuffers de 16 bits (revisar si ya) · B8 saltar dibujo de cuadros.
- ⬜ Traductor y build: A14 higiene · A10 -O2 solo en lo caliente · A11 contador de ciclos de 32 bits · A12 quitar ramas FPSCR.PR.
- ⬜ T3 microbenchmark de costos unitarios.
- ⬜ Lo que paga el build de entrega sin necesitarlo: `write_hash`, contadores A9 y el log a ms0: (que abre y cierra el archivo por línea, C10) pasan a opcionales por cfg o por build.
- ⬜ Costo del AICA en la PSP (se emula entero y su salida se tira): medirlo con los contadores y decidir A7 con ese número.
- ⬜ Otros bucles de espera para `[idle]` (carga, GD-ROM, pausa, cambio de capítulo): perfilar dónde gira el guest.
- ⬜ B6 completo: texturas a 16 bits y CLUT del GE para las de paleta.
- ⬜ Ritmo: esperar vblank al presentar y no pasar de 60 Hz (fila 7 de la tabla del diseño; `clock_wait_vblank` existe sin uso).

### Fase 4 · Juego completo
- ⬜ 7 audio (AICA: A7 reducido, A6b en lotes, B9 Media Engine; C9 heap con 1 MB fuera, revisar al crear el hilo de audio) y medida de fps · los 5 capítulos, jefes, memory card (recuperar `VMU.BIN.TMP` tras un corte de energía) · 10 disco real con un solo comando · T texturas HD de Steam (después).

### Fase 5 · PSP real
- ⬜ T4 prueba en la PSP-3001 de destino (custom firmware ARK/PRO, DISCO.ISO de ~1 GB en Memory Stick) · B10 orden del código caliente · B11 scratchpad · riesgos solo-hardware de la revisión de video (x negativa en through, alfa/stencil, vblank, dcache) · C13 que el Allegrex respete RM y FS (sonda de redondeo en hardware) · HOME y suspensión reales · presentación en pantalla (hasta hoy solo captura) · costo de escribir la VMU (128 KB por bloque) y C8 lectura del disco por bloques en vez de sector a sector.

### Fase 6 · Red de pruebas completa (al final, parte del proyecto)
- ⬜ Pruebas sólidas por caso de uso de todo lo construido (imagen contra Flycast por escena, correctitud del build double, velocidad, controles, audio), con su contraprueba; reusar y ordenar las que ya existen. De la auditoría: retirar `test-psp-geometria` y `comparar-geometria` (la IoU pasa casi por construcción); `comparar-titulo` → `comparar-escena`; `test-psp-texturas-juego (b)` con referencia fija para que pueda fallar.

## Tabla del diseño: estado por pieza

La tabla 1 de `docs/diseno-psp.md` §1 (cada pieza del PC que se sustituye en la PSP), con su estado de hoy.

| # | Pieza | Qué se diseñó para la PSP | Estado hoy | Dónde sigue |
|---|---|---|---|---|
| 1 | Arranque del GE | `sceGuInit` + lista de comandos en RAM | ✅ | — |
| 2 | Dibujo | `sceGuDrawArray` por lista (opaca, punch-through, translúcida), mezclas y niebla | ✅ las tres listas, recorte de usuario y niebla; ⚠ sin culling, profundidad lineal, sin modifier volumes ni color offset | Fase 2 (6d) · Fase 3 (B1, B3, B5, B7) |
| 3 | Texturas | decodificar a 16 bits, swizzle para el GE, caché acotada en RAM + eDRAM | ⚠ caché por páginas sucias (2,5 MB) hecha; se decodifican a 32 bits y las de paleta se expanden | Fase 3 (B6) |
| 4 | Presentación | directo a 480×272, doble búfer de 16 bits | ✅ directo y doble búfer; ⚠ búferes de 32 bits, sin esperar vblank | Fase 3 (B2, ritmo) |
| 5 | Controles | `sceCtrlPeekBufferPositive` cada cuadro → mando Maple | ❌ no se leía ningún botón | 🔄 Fase 1.5 (T5, en curso) |
| 6 | Audio | hilo con `sceAudioSRCOutputBlocking` (y AICA en el Media Engine) | ❌ sin audio (opción D del diseño, primer hito); el AICA se emula igual y su salida se tira | Fase 3 (medir su costo, A7) · Fase 4 (audio) |
| 7 | Reloj y ritmo | `sceDisplayWaitVblankStart` a 60 Hz | ❌ sin ritmo (si algún día va más rápido que el Dreamcast, irá acelerado) | Fase 3 (ritmo) |
| 8 | Disco | `sceIo` sobre una imagen en `ms0:` | ✅ `DISCO.ISO` volcado del área HD real; ⚠ lee sector por sector; falta el comando único para el usuario | Fase 4 (fila 10) · Fase 5 (C8) |
| 9 | Tarjeta de memoria | `sceIo` en `ms0:`, temporal + reemplazo | ✅ escritura atómica; ⚠ no recupera `VMU.BIN.TMP` tras un corte de energía | Fase 4 |
| 10 | Archivos y diagnóstico | leer de `ms0:`; quitar los volcados en el build PSP | ✅ carpeta tomada de `argv[0]`; ⚠ log cada 60 cuadros, hash de escrituras y contadores siempre activos | Fase 3 (build de entrega sin eso) |
| 11 | Log del ABI | `sceIo` o nada | ✅ nada | — |
| 12 | Memoria emulada | un bloque contiguo; presupuesto ≈44–53 MB de 52 | ⚠ seis bloques sueltos; la memoria total nunca se midió con el EBOOT actual | Fase 1.5 (medirla) |
| 13 | Proceso | 333 MHz y VFPU en el hilo principal | ✅ | — |

## Filas

En este orden. Antes de tocar una, escribe su caso de uso: qué se ejecuta y qué se debe ver. Cada fila tiene su CIERRA CUANDO, medido corriendo; el piso de todas: su evidencia (captura, log, número) repetida por ti, la negativa medida (sin el cambio de la fila, falla), la revisión semántica (PRUEBAS §7) y su PR mergeado.
0. ✅ framework (PR #2) — Con qué recompilador de Dreamcast seguir: dcrecomp, dream-recomp y cualquier otro vivo, cada uno corriendo Ikaruga con el mismo disco hasta donde llegue.
   CIERRA CUANDO: `docs/framework-comparacion.md` con, por framework, hasta dónde llega Ikaruga (medido por ti: log, dirección o captura), qué hardware implementa (texturas PVR2, AICA, MMU/store queues, GD-ROM), lenguaje, dependencias de host y licencia, cada afirmación marcada CONFIRMADO/CORREGIDO/SEGÚN; y la decisión escrita de con cuál seguir y por qué, apoyada solo en lo CONFIRMADO. Si gana uno distinto de dcrecomp, las filas 1–7 se reescriben sobre él en esa misma ronda.
1. ✅ docs (PR #4) — En `docs/`: cómo extraer y recompilar Ikaruga con el framework elegido (comandos reproducibles, sin datos del juego), y el diseño de la capa PSP sobre el runtime de dream-recomp: qué módulos dependen del host (Vulkan, SDL3) y cómo se sustituyen (PVR → sceGu, AICA/ARM7 → Media Engine o HLE, GD-ROM → archivos en `ms0:`, Maple → controles de la PSP), presupuesto de RAM en 64 MB (código + RAM emulada de 16+8+2 MB) y modo de pantalla (TATE 272×363 vs. horizontal 204×272).
   CIERRA CUANDO: siguiendo `docs/` en un directorio limpio, con el disco copiado, sale el mismo resultado (código generado o binario, comparado por hash); y el diseño nombra para cada pieza qué se toma de sf64-psp/oot-psp con archivo:línea que tú abriste y confirmaste.
2. ✅ pc (cerrada con la medición de la fila 0) — Ikaruga recompilado corre en PC (Linux) hasta dibujar algo reconocible del juego (logo, título o texto) con texturas; si el framework no tiene texturas, se implementan en nuestro repo o como parche documentado.
   CIERRA CUANDO: captura de la ventana con un elemento gráfico de Ikaruga texturizado, más el log del arranque; y la negativa: sin el parche de texturas la misma escena sale sin ellas (si el framework ya las trae, se dice así y se muestra la captura).
3. ✅ toolchain (PR #3) — Toolchain de PSP fijado y reproducible en Docker (PSPDEV con versión y SHA-256 fijados, como `oot-psp/psp.sh`), con un EBOOT mínimo nuestro que corre en PPSSPP sin ventana.
   CIERRA CUANDO: `scripts/build-psp.sh` produce `EBOOT.PBP` desde cero en un contenedor limpio, y PPSSPP headless lo arranca y deja una captura con texto nuestro en pantalla; la negativa: un EBOOT roto a propósito no muestra ese texto.
4. ✅ psp-compila (PR #6: -Os cabe, 13.65 MB + 28.1 MB de guest; -O2 no, 24.65 MB) — El C++ generado de Ikaruga + el runtime de dream-recomp compilan para Allegrex (MIPS) con `psp-g++` de ese toolchain, sin la capa gráfica ni de audio (stubs).
   CIERRA CUANDO: el `.elf` enlaza; tamaño de `.text`/`.data`/`.bss` frente a 24 MB (PSP-1000) y 52 MB (2000+) y, si no cabe, plan de overlays en `docs/` con los números.
4b. ✅ medir-pc (PR #5) — Con números de Ikaruga (no de Crazy Taxi): velocidad en gameplay, perfil (`perf`) con el % de acceso a memoria, llamadas indirectas, código generado, AICA y PVR; uso de FPU doble/FIPR/FTRV; cobertura de funciones en título + capítulo 1.
   CIERRA CUANDO: `docs/medicion-pc.md` con cada número y su comando, repetido por ti al menos en la velocidad y el top del perfil.
5a. ✅ psp-arranca-headless (PR #9: arranca en PPSSPP, mismos ciclos/PC que PC en cuadros 60–180, cuadro 600 = título, 0,036x tiempo real) — EBOOT -Os en PPSSPP que carga el juego de `ms0:` y corre sin video, con log de cuadros/ciclos/tiempo: primera velocidad en la PSP.
   CIERRA CUANDO: `docs/arranque-psp.md` con cuadros alcanzados y veces tiempo real en PPSSPP, repetido por ti; negativa: sin `1ST_READ.BIN` sale con error claro.
5b. ✅ idle-skip (PR #10, medido en PC: mismo guest, ≈mitad de host; pendiente cablearlo al build de PSP, va con la fila 7) — Saltar la espera de `kmWaitVBlank` adelantando el guest al próximo evento; parche `patches/dream-recomp/0004-*`.
   CIERRA CUANDO: en PC, con y sin idle-skip, guest idéntico (ciclos, cuadros, PC) y host menor, 3 corridas cada uno, repetido por ti; captura del capítulo 1 igual.
5d. ✅ idle-skip en PSP (PR #12: ≈5x más rápido en PPSSPP, 0,029x→0,15x; repetido: 120 s → cuadro 840 contra 300) — runtime con la parte del parche + C++ regenerado con el traductor parchado (C++ regenerado con el traductor parchado, sha256 924f8b67…); medir en PPSSPP con y sin.
   CIERRA CUANDO: mismos ciclos/PC por cuadro que sin idle-skip y más cuadros por segundo en PPSSPP, repetido por ti.
5c. ✅ psp-geometria (PR #19 cd8b6c4; repetido por mí: IoU 0,6068, 947=947 polígonos, 8/8 pruebas, write_hash igual 60…600) (antes fila 5) Capa de video mínima sobre 5a hasta ver geometría de Ikaruga en PPSSPP, aunque sea sin texturas.
5. (partida en 5a/5b/5c) psp-arranca — Capa de plataforma PSP mínima (video 480×272, entrada, archivos desde `ms0:` con los datos que el usuario extrae de su disco) hasta que el juego corre en PPSSPP y llega al primer cuadro, aunque sea sin texturas.
   CIERRA CUANDO: captura de PPSSPP headless con geometría de Ikaruga en pantalla y log del juego avanzando más allá de la inicialización (dirección o función alcanzada).
   Nota de revisión de 5c: la prueba de IoU ≥ 0,6 que diseñé pasa casi por construcción sin texturas (la PSP cubre el 94 % de la pantalla). Lo fuerte es el conteo de polígonos y la corriente del TA idénticos. En la fila 6 la comparación debe ser de píxeles contra la captura de PC (con texturas), no de máscaras.
6. ✅ psp-titulo (PR #21 6b98ee9; repetido por mí: PSNR 28,43 dB vs PC en el cuadro 1243, 9,46 sin texturas, 7,37 contra otra escena, 8/8 pruebas; revisado lado a lado) — Backend del PVR de dream-recomp sobre sceGu en lugar de Vulkan, con texturas (paletas, formatos, listas opacas/translúcidas/punch-through), con el backend GU de sf64-psp (CC0, se puede copiar) y el enfoque de `gfx_scegu.c`/`psp_texture_manager.c` de oot-psp (sin licencia: se imita, no se copia). El juego es 640×480 y la PSP 480×272: el diseño decide el escalado (y el modo vertical/TATE).
   CIERRA CUANDO: captura de PPSSPP con la pantalla de título de Ikaruga texturizada y legible, junto a la misma escena en PC (fila 2), y coinciden a la vista.
6b. costados-hud (causa confirmada: `video.cpp` ignora el recorte de usuario `tile_clip`/`clip_x0..y1` que PC aplica en `geometry.frag:61-66`) — En el capítulo 1 (cuadro 2600) la PSP dibuja el escenario en las franjas laterales que en PC son negras con el HUD (1P, CHAIN STATUS, vidas): solo salen dos barras negras delgadas al borde del área de juego. Los polígonos opacos negros que tapan los costados no se dibujan bien (orden, z, mezcla o recorte del modo 2D). Prueba: PSNR por regiones del cuadro 2600 contra PC, con los costados como región propia; contraprueba con el arreglo revertido. Visto en la corrida de la fila 6.

6c. render-a-textura — La PSP no implementa el render a textura del PVR2 (`FB_W_SOF1` bit 24; PC: `framebuffer.cpp:211`, `boot_main.cpp` write_back/write_texture): esa VRAM nunca recibe el render y además cada render se presenta en pantalla. Causa probable de los botones en blanco de SELECT GAME MODE (cuadro ~1700). Confirmado en código (revisión independiente 2026-11-10); que Ikaruga lo use ahí, sin medir.
6d. fidelidad-del-video (sospechas de la revisión independiente, cada una se mide antes de arreglar): profundidad lineal en 16 bits (PC usa log2(1+1e5·inv_w)/34) → z-fighting; texturas afines sin corrección de perspectiva en modo through; culling siempre apagado (`video.cpp:112`, confirmado); sin niebla, color offset ni modifier volumes; alfa destino sin stencil limpio (PSP real); fondo con x negativa en modo through (PSP real); sin espera de vblank entre renders (PSP real); texturas stride sin TEXT_CONTROL; DAZ en el build `float` (el SH-4 con DN=1 aplana denormales al leer y la FPU de la PSP no; la 8b lo corrigió solo en `double`; en `float` cuesta ~4 % y el efecto es ~1e-35: corregir solo si Flycast muestra diferencia).

7. audio-y-medida — AICA: el camino diseñado (Media Engine) con un stub que no bloquee; fps medidos en PPSSPP en el título y, si se llega, en la primera escena jugable (PPSSPP no es hardware real).
   CIERRA CUANDO: tabla en `docs/` con fps por escena y estado del audio, y lista priorizada de lo que falta para hardware real.
Objetivo de hardware: la consola de destino es una **PSP-3001** (64 MB, 333 MHz, pantalla 480×272); `MEMSIZE=1`, como sf64-psp y oot-psp. La PSP-1000 (32 MB) queda fuera.
8. ✅ correctitud-psp (PR #16: 7 bugs con prueba; write_hash igual PC↔PSP 60…600 con idle-skip; repetido desde cero) (de `docs/revision-optimizaciones.md`, PR #11) — Bugs que hacen que la PSP no corra igual que PC: redondeo del guest (`fesetround` de newlib-psp es un stub → escribir RM del FCSR con `ctc1` en el parche 0001), RTC sembrado con `time()` (rompe la comparación determinista con PC: sembrar como `--rtc-seed`), callback de salida (HOME), reabrir el disco tras suspender, `ms0:` fijo en vez de salir de `argv[0]`, la VMU que nunca se guarda. Oráculo propuesto: el `write_hash` de `DcMemory`, comparado PC↔PSP por cuadro.
   CIERRA CUANDO: el `write_hash` coincide PC↔PSP en los cuadros del log hasta el título, repetido por ti; HOME sale; la VMU se escribe en ms0: y se relee.
8b. divergencia-1983 (entregada: causa = la PSP no aplana denormales al leer + propagación de NaN; parche 0010 soft-DAZ en `double`; según agente, double igual 60…2600; pendiente mi batería) — Con las pulsaciones del capítulo 1, PC y PSP escriben distinto desde el cuadro 1983 también con FPU `double` (según el agente de la 9b; sin verificar). Con `double` la igualdad debe ser total: o es un bug del port o una diferencia en cómo se inyectan las pulsaciones (cuadro de entrada, polling de Maple). Prueba: `test-psp-igual-pc.sh` con build `DREAM_FPU_DOUBLE=1` y `PULSAR=` del capítulo 1 hasta el cuadro 2600, `hash_every=1`; localizar la primera escritura distinta.

9. (9a ✅ PR #18: runtime -O2 + generado -Os, 1,3x) velocidad-psp (después de ver el título, de la misma revisión) — En orden: runtime y stubs a -O2 con el generado a -Os (A1, +85 KB); FIPR/FTRV en `float` o VFPU en vez de `double` emulado (A3, medir antes cuántas ejecuta el juego, A9); camino de las store queues (A5); scheduler: solo las líneas útiles del SPG y AICA en lotes (A6); costo de los 204 `throw`/s en la PSP (A13). Para el video de 5c: `sceGuSync` bloqueante, la lista GE de 1 MB sin control de desborde, búferes en 5650 para liberar eDRAM.
   CIERRA CUANDO: cada cambio con su número antes/después en PPSSPP y el mismo `write_hash` por cuadro.
9b. fpu-float — FIPR/FTRV/FMAC en `float` (o VFPU con el mismo resultado) en PC y en PSP por igual, con `double` como opción de compilación para comparar. Medido en la fila 9: esas tres operaciones son ~43 % del tiempo del capítulo 1 (0,0518x → 0,0902x con float). El `double` actual no es el hardware (el SH-4 calcula FIPR/FTRV con su propia aproximación), así que el patrón PC↔PSP se conserva cambiando los dos lados con el mismo código; la fidelidad al Dreamcast la mide la fila E (Flycast). Prueba: `test-psp-igual-pc.sh` verde con float en los dos lados, roja si solo un lado cambia; título ≥ 25 dB; cuadro 2600 ≥ 0,085x.

10. disco-real — `scripts/preparar-disco-ikaruga.sh` (genisoimage sobre `fs/`) NO reproduce el disco: deja los archivos en otros sectores y el juego diverge de PC desde el cuadro 4 (hallado en la fila 8). El camino correcto es volcar el área HD del CHD real (`tools/volcar-area-hd.cpp`). Hacerlo el camino por defecto para el usuario (sin compilar contra dream-recomp a mano) y retirar el ISO armado.
   CIERRA CUANDO: el usuario genera `DISCO.ISO` desde su CHD con un solo comando documentado y `scripts/test-psp-igual-pc.sh` pasa con él.
E. referencia-emulador (prioridad: justo después de integrar 9b y 6b+6c) — Oráculo independiente del recompilador: capturas de Flycast (mismo disco real) en las mismas escenas (título quieto, logo, capítulo 1) comparadas contra PC recompilado y contra la PSP. El PC recompilado sigue siendo el patrón de cuadro exacto (mismo código y write_hash); Flycast caza lo que dream-recomp dibuje mal en los dos lados (niebla, mezclas, formatos), que la comparación PC↔PSP no puede ver. Alinear por escena o por VBlanks desde el arranque, con métrica tolerante (PSNR/SSIM) y umbral fijado antes de medir; lo que difiera se reporta como bug de dream-recomp con su parche en `patches/dream-recomp/`.

R. referencia-pc — La versión de Steam (appid 253750, Windows, corre por Proton) es la referencia visual y de comportamiento: dónde tenemos que llegar.
   Avance: logo, menú de modos e inicio del capítulo 1 capturados. Medido en la captura: el área de juego es vertical 3:4 (810×1080) con paneles a los lados. En la PSP (480×272) eso deja 204×272 en horizontal; con la PSP de lado (modo TATE) queda 272×363.
   Medido en el recompilado de PC (`dr-rev`, `--press start@700,start@1000 --screenshot-at 1150`): en modo horizontal el título dibuja una columna vertical ≈360×480 al centro de 640×480, con barras negras. Falta: cómo dibuja el modo TATE de las opciones del juego (si rota y llena los 640×480).
   CIERRA CUANDO: capturas locales (nunca en el repo) del título, el menú, el inicio del capítulo 1 y el modo vertical; y en `docs/` la lista de lo que esas escenas exigen al port (capas, efectos, orientación), sin imágenes.
T. texturas-hd (después de la fila 6) — Usar las texturas HD de la versión de Steam (`tex.arc`, 211 MB, contenedor NARC sin descifrar) donde quepan sin romper la PSP: identificar el formato de NARC, emparejar cada textura HD con su equivalente del disco de Dreamcast, y reducirlas al tamaño que use la PSP (la VRAM son 2 MB y la caché de texturas de sf64-psp usa ~1.2 MB). Candidatas: fuentes, HUD, menús y fondos, donde reducir desde HD se ve mejor que la textura original. Se extraen en la máquina del usuario desde su propia copia de Steam, como el disco; nunca en el repo.
   CIERRA CUANDO: tabla en `docs/` de qué texturas se reemplazan y su tamaño en VRAM; captura en PPSSPP de la misma escena con y sin ellas; fps sin caída medible y sin errores de memoria en una partida del capítulo 1.

## Camino a la PSP real
 (el grueso del trabajo que falta, no un renglón al final):
- Velocidad: ~5 fps en PPSSPP con la 9b; jugable pide 30 (6–12× más). Lo barato ya se hizo; queda VFPU, Media Engine, acceso a memoria del guest directo y optimizar el código que genera el recompilador. Meses y sin garantía en 333 MHz.
- PPSSPP no es el hardware: su velocidad no predice la de la PSP-3001 y hay riesgos que solo aparecen en hardware (coordenadas negativas en through, alfa/stencil, vblank, dcache).
- Juego completo: audio (AICA), TATE, controles, los 5 capítulos, jefes y la memory card.
- Logística: la PSP de destino necesita custom firmware (ARK/PRO) para un EBOOT casero; leer un DISCO.ISO de ~1 GB desde Memory Stick; alguien con la PSP en la mano prueba cada versión.
- Meta realista para Navidad: verlo arrancar en su PSP (título, menú, capítulo 1 a pocos fps). Jugarlo bien es proyecto de meses.

