# Diseño de la capa PSP: qué del runtime de dream-recomp se sustituye, con qué y con qué presupuesto

Base: dream-recomp `51ca2766d2ebeae9caacda4e305eacb88cf64013` (ver `docs/reproducir-pc.md`).
Rutas `dr-rev/...` = árbol medido `~/code/ajenos/dr-rev` (ese commit más un archivo de notas).
Rutas `sf64/...` = `~/code/ajenos/sf64-psp` (CC0 salvo `src/psp/audio_me.c`, GPL: solo se describe).
Rutas `oot/...` = `~/code/ajenos/oot-psp` (sin licencia, sin archivo `LICENSE*`: CONFIRMADO con `ls`;
su código no se copia, solo se describe el enfoque).

Etiquetas: **CONFIRMADO** (abierto o corrido: se dice dónde), **SEGÚN <fuente>**, **[INFER]**.
Los números de los estudios de dream-recomp se midieron con Crazy Taxi en un Apple M4, no con Ikaruga.

## 0. Qué se queda igual

- El código traducido del SH-4 (C++ generado en `gen/`, 20 archivos) y el núcleo del runtime
  (memoria, SH-4, scheduler, Holly, PVR por software, AICA, Maple, GD-ROM lógico, HLE de BIOS) son
  C++ sin dependencias de host salvo las de la tabla 1. CONFIRMADO: `grep` de Vulkan/SDL/hilos/
  archivos/reloj sobre `dr-rev/runtime`, `render`, `audio` solo da los archivos de la tabla 1.
- El intérprete SH-4 no hace falta: el release sin intérprete juega el Capítulo 1 con
  `untranslated call targets: 0` (SEGÚN `dr-rev/RESULTADO-IKARUGA.md`).
- Decodificación de listas de pantalla y texturas sin GPU: `dr-rev/render/src/display_list.cpp`,
  `render/src/texture.cpp` (twiddle `texture.cpp:94`, VQ `texture.cpp:30`; CONFIRMADO). La librería
  `dream::render` es independiente del dispositivo (SEGÚN `dr-rev/docs/runtime-render.md:11-17`).
- Hilos: ninguno. CONFIRMADO: `grep std::thread|pthread_create|SDL_CreateThread|std::async` sobre
  `runtime/ render/ audio/` (sin tests) no devuelve nada. El runtime es de un hilo; el único
  paralelismo es el interno de SDL para el audio [INFER].

## 1. Módulos dependientes del host

| # | Módulo (dr-rev, archivo:línea, CONFIRMADO) | Qué hace | En la PSP | Idea tomada de (archivo:línea abierto) |
|---|---|---|---|---|
| 1 | Contexto Vulkan: `render/src/vk/context.cpp:78` `vkCreateInstance`, `:193` `vkCreateDevice` | instancia/dispositivo | `sceGuInit` + lista de comandos GE en RAM | sf64 `src/psp/renderer.c:2001` `sceGuInit` |
| 2 | Renderer: `render/src/vk/renderer.cpp:381` pipelines, `:565` `vkCmdDraw` por polígono/tira | dibuja las listas del TA decodificadas | `sceGuStart(GU_DIRECT)` + `sceGuDrawArray` por lista (opaca, punch-through, translúcida); modos de mezcla PVR → `sceGuBlendFunc`; niebla de tabla → niebla lineal del GE o color por vértice [INFER] | sf64 `renderer.c:1156` `sceGuStart`, `:1158` `sceGuDrawBuffer`; oot `src/port/psp/gfx/gfx_scegu.c:367` `sceGuStart`, `:1802` `sceGuDrawArray` con transformación hecha en CPU/VFPU |
| 3 | Caché de texturas: `render/src/vk/texture_cache.cpp:171` un `vkAllocateMemory` por textura, `:232` `decode_texture` | decodifica twiddled/VQ/paletas a RGBA8888 y sube | decodificar a 16 bits (4444/5551/565) y **swizzlear** para el GE; caché acotada en RAM + lo que quepa en los 2 MB de eDRAM | sf64 `renderer.c:781-782` `sceGuTexMode`/`sceGuTexImage`; oot `gfx/psp_texture_manager.c:221` `swizzle_fast`, `:335` comprobación de espacio en VRAM |
| 4 | Offscreen + presentación: `render/src/vk/offscreen.cpp`, `present.cpp:168` pipeline, `:372` triángulo a pantalla completa; FIFO a la tasa del monitor (SEGÚN `dr-rev/docs/cpu-performance-study.md:49-51`; CONFIRMADO `present.cpp:385` `VK_PRESENT_MODE_FIFO_KHR`) | dibuja 640×480 aparte y escala a la ventana | dibujar directo al framebuffer de 480×272 con la escala ya en la proyección; sin pasada extra; doble búfer `sceGuSwapBuffers` | sf64 `renderer.c:1159` `sceGuDispBuffer`, `:1194` `sceGuSwapBuffers` |
| 5 | Ventana y entrada SDL3: `render/src/vk/window.cpp:50` `SDL_Init`, `:54` `SDL_CreateWindow`, `:68` superficie Vulkan, `:309` mandos, `:377` `SDL_PollEvent`, `:444` `SDL_GetKeyboardState` | ventana, teclado/mando → controlador Maple | sin ventana; `sceCtrlPeekBufferPositive` cada frame → estado del mando Maple (Maple no cambia) | sf64 `src/psp/input.c:75-76` `sceCtrlSetSamplingCycle/Mode(ANALOG)`, `:86` `sceCtrlPeekBufferPositive` |
| 6 | Audio: `audio/src/sdl_sink.cpp:17` `SDL_InitSubSystem(AUDIO)`, `:25` stream 16 bit estéreo 44,1 kHz, `:52` cola, `:80` `SDL_PutAudioStreamData`; bloques de 512 y descarte por nivel alto (SEGÚN `dr-rev/docs/runtime-aica.md:90`, `:98`) | cola de muestras del mezclador AICA al dispositivo | hilo de audio con `sceAudioSRCChReserve` + `sceAudioSRCOutputBlocking`; misma política de cola/descarte | sf64 `src/psp/audio_output.c:300` `sceAudioSRCChReserve`, `:307` `sceKernelCreateThread`, `:8-12` 32 kHz, 8 bloques, objetivo 3, 1152 frames; oot `oot_psp_audio_backend.c:2040` `sceAudioSRCChReserve`, `:1462` `sceAudioSRCOutputBlocking` |
| 7 | Reloj y ritmo: `runtime/boot/boot_main.cpp:1764-1801` `steady_clock` + `sleep_for` (pausa `:1785`, espera al reloj del guest `:1801`); fps `:529`; medida final `:2055`, `:2100` | ata el reloj virtual del guest al de pared | `sceKernelGetSystemTimeWide` + `sceDisplayWaitVblankStart` (60 Hz = ritmo del Dreamcast NTSC); sin `sleep` fino | `sceKernelDelayThread` como espera: sf64 `audio_me.c:6078` (solo la llamada); hilo de video con semáforos: oot `oot_psp_video_thread.c:44` `sceKernelCreateThread`, `:18`/`:20` `sceKernelWaitSema`/`SignalSema` |
| 8 | Disco GD-ROM: `runtime/src/gdrom/disc.cpp:109` lee el índice, `:166` y `:356` `fopen` de pistas/imagen | sirve sectores al GD-ROM emulado | `sceIoOpen`/`sceIoLseek`/`sceIoRead` sobre `ms0:/PSP/GAME/IKARUGA/` con los archivos extraídos o una imagen sin comprimir; CHD/libchdr no [INFER: zlib/lzma por sector cuesta CPU] | oot `src/port/psp/oot_psp_asset_loader.c:569` `sceIoOpen` de un paquete de assets generado en la primera corrida |
| 9 | Tarjeta de memoria: `runtime/src/maple/maple.cpp:47` `ifstream`, `:60` `ofstream` del `.vmu` | carga/guarda la VMU | `sceIo` en `ms0:`, escritura a temporal y reemplazo | sf64 `src/psp/settings.c:75-80` lectura `sceIoOpen/Read/Close`, `:131-135` `sceIoRemove` + escritura |
| 10 | Arranque: `boot_main.cpp:1421` lee `1ST_READ.BIN`, `:1432` BIOS opcional, `:1447` flash, `:502`/`:516` bindings, `:2327` reporte; y ~20 `fopen` de volcados de depuración (`:578`-`:2305`) | carga de archivos y diagnóstico | `sceIo` desde `ms0:`; quitar los volcados en el build PSP | idem 9 |
| 11 | Log del ABI: `runtime/src/sh4/abi_bare.cpp:380` `fopen` | archivo de log opcional | `sceIo` o nada | — |
| 12 | Memoria emulada: `runtime/include/dream/runtime/mem/dc_memory.h:114-119` tamaños, `runtime/src/mem/dc_memory.cpp:114-119` seis `new uint8_t[N]()` | RAM/VRAM/ARAM/ROM/flash/OCRAM del guest | un bloque contiguo alineado; ver §2 | heap a todo lo libre: sf64 `src/psp/main.c:25` `PSP_HEAP_SIZE_KB(-1)`; 64 MB: sf64 `Makefile:647`, oot `Makefile:1852` `mksfoex -d MEMSIZE=1` |
| 13 | Proceso: reloj de CPU y VFPU | — | `scePowerSetClockFrequency(333,333,166)`; hilo principal con `PSP_THREAD_ATTR_VFPU` | sf64 `src/psp/main.c:78` y `:23`; oot `gfx/gfx_window_psp.c:18` |

Nota: `memory-footprint-study.md` cita `dc_memory.h:93-98` y `dc_memory.cpp:61-67`; en este commit
las mismas líneas están en `:114-119` (CONFIRMADO con `grep`).

## 2. (a) Presupuesto de RAM (PSP-2000+, 64 MB, ≈52 MB útiles)

| Concepto | Tamaño | Fuente |
|---|---|---|
| RAM principal SH-4 | 16 MB | CONFIRMADO `dc_memory.h:114` |
| VRAM PVR | 8 MB (un búfer, dos vistas) | CONFIRMADO `dc_memory.h:115`; SEGÚN `memory-footprint-study.md` §1.2 |
| RAM de sonido (AICA) | 2 MB | CONFIRMADO `dc_memory.h:116` |
| Boot ROM | 2 MB | CONFIRMADO `dc_memory.h:117` |
| Flash + OCRAM | 136 KB | CONFIRMADO `dc_memory.h:118-119` |
| **Guest total** | **28,1 MB**, residente desde el inicio (relleno a cero) | SEGÚN `memory-footprint-study.md` §1.1, §1.3 |
| Código traducido | **estimación 13–20 MB** | [INFER] a partir de `text` x86-64 = 13 146 938 B (CONFIRMADO `size` del release); MIPS de 32 bits fijos suele salir igual o 1,5× mayor que x86-64 |
| data + bss del binario | ≈0,8 MB | CONFIRMADO `size`: 227 088 + 590 184 B |
| Caché de texturas decodificadas | 1–2 MB | [INFER]: el título de Crazy Taxi usa 34 texturas, 0,82 MB en RGBA8888 (SEGÚN `memory-footprint-study.md` §0); a 16 bits, la mitad |
| Lista GE (×2), audio, pilas, newlib | ≈1–2 MB | [INFER] |
| Framebuffers | 0 de RAM principal: van en los 2 MB de eDRAM (480×272×2 B×2 + Z ≈ 0,8 MB) | [INFER] |
| **Suma** | **≈44–53 MB** de ≈52 | [INFER]: no hay margen si el código sale grande |

Recortes posibles, de más a menos seguro:

1. **Boot ROM, 2 MB.** Ikaruga corre sin `--bios` (SEGÚN `RESULTADO-IKARUGA.md`); la BIOS es HLE.
   Dejar una página que devuelva ceros para las lecturas. El estudio no lo recomienda en PC (§1.4)
   porque generaliza a otros títulos; aquí solo importa Ikaruga [INFER].
2. **Código no ejecutado.** Medir cobertura en PC (qué funciones traducidas se llaman en los 5
   capítulos) y compilar con `-Os`; lo frío, a un PRX/overlay cargado bajo demanda [INFER].
   Las variantes `ikaruga_reloc_*` de `gen/` (CONFIRMADO `ls`) son candidatas obvias a overlay si
   solo se usan en ciertas fases [INFER].
3. **ARAM, 2 MB**, solo si el audio es HLE (ver §5); con ARM7 emulado no se puede tocar.
4. **RAM principal y VRAM no**: el juego direcciona todo; recortarlas cambia el comportamiento.

En PSP-1000 (32 MB, ≈24 útiles) no cabe ni la memoria del guest más el código [INFER].

## 3. (b) CPU

SEGÚN `dr-rev/docs/cpu-performance-study.md` (Crazy Taxi, modo demo, release en Apple M4):

- Release headless: 3,5× tiempo real (§0, l.31).
- **Acceso a memoria:** cada `read32` es una llamada virtual → `DcMemory::read32` → `load` →
  `resolve`, que rehace la región en cada acceso (escaneos lineales de P4 y de MMIO, `switch` de
  región, struct de 24 B): una llamada indirecta, una fuera de línea y una docena de saltos para
  leer RAM (§3.1, l.208-258). Es el **56 % del perfil** (§0, l.38); `resolve` solo, 40,9 %
  (§1.1). Inlinear un camino rápido de RAM por máscara+base: 3,5× → 5,8× (§0).
  CONFIRMADO en este commit: `resolve` en `dc_memory.cpp:149`, `load` `:229`, `read32` `:296`.
- **Llamadas indirectas:** 5,9 M por segundo de guest, una cada ~34 ciclos del SH-4; cada una era
  una búsqueda binaria en una tabla de 3 794 entradas (§0, §1.2, §5). Una caché directa:
  5,8× → 10,0×. CONFIRMADO: este commit ya trae esa caché, `runtime/src/sh4/abi_bare.cpp:35-57`.
- AICA ≈10 % del perfil (DSP 4,7 %, ARM7 2,8 %); ARM7 ≈3 M instrucciones/s (§1.1, §1.2).
- El código emitido es solo ~13 % del perfil en el modo demo; en gameplay pesa más y la mejora
  sería menor (§0, l.60).
- Medido con Ikaruga en x86: 5,1× headless con gameplay (SEGÚN `docs/framework-comparacion.md:19`).

Implicación para un Allegrex a 333 MHz [INFER, nada medido en PSP]:

- Un núcleo x86 actual hace del orden de 20–50× más trabajo por segundo que un Allegrex
  (MIPS en orden, un solo issue, 333 MHz, caché de 16+16 KB). Con 5× de margen en x86 queda
  **≈0,1–0,25× tiempo real** en la PSP sin optimizar: 6–15 fps.
- Solo las llamadas indirectas (5,9 M/s) a ~20 ciclos cada una ya son ~35 % de los 333 MHz.
- Obligatorio antes de medir nada en serio: (1) camino rápido de RAM inline
  (`ram_base + (a & 0x00FFFFFF)` si la región es área 3), (2) caché de destinos indirectos (ya
  está), (3) sacar el AICA del núcleo principal (§5), (4) `-O2` en el código generado y tratar de
  no usar excepciones C++ en la ruta caliente.
- La FPU del Allegrex es de precisión simple; si el juego usa el modo doble del SH-4
  (`FPSCR.PR=1`) hay que emularlo por software. FIPR/FTRV → VFPU. No revisé qué modo usa Ikaruga.
- 60 fps a plena lógica es dudoso. Salida realista: lógica a 60 Hz de guest con frameskip de
  render, o aceptar ralentización.

## 4. (c) Pantalla

PSP: 480×272. Área de juego de Ikaruga: 3:4 (vertical).

| Modo | Área | Píxeles | Escala respecto a 480 líneas del Dreamcast |
|---|---|---|---|
| Horizontal | 204×272 (272·3/4) | 55 488 | 0,567 |
| TATE (consola de lado) | 272×363 (272·4/3) | 98 736 | 0,567 sobre el lado de 480 en TATE del juego |

**Recomendación: TATE** [INFER]. 1,78× más píxeles para el área de juego; las balas y los
cambios de polaridad se leen mejor; no sobra pantalla negra (horizontal deja 276 columnas sin
uso). El GE rota gratis: es cambiar la matriz de proyección, sin pasada extra. Costo: la cruceta
queda de lado y hay que remapear `sceCtrl` (arriba↔derecha, etc.) y elegir botones para disparo y
polaridad que caigan bajo los pulgares. Dejar el horizontal como opción (menú/archivo de
configuración) para quien juegue sin girar la consola. Primero hay que confirmar en PC que el modo
TATE del juego dibuja el área a lo largo de los 640 px; si no, la rotación se hace en la capa PSP
sobre el framebuffer de 640×480.

## 5. (d) Audio: AICA/ARM7

Hoy: el ARM7 se emula con el intérprete de Flycast (`runtime/src/aica/arm7.cpp`, tabla
`arm7_ops.inc`), con un tick del scheduler cada 4 535 ciclos SH-4 que corre 512 ciclos ARM
(SEGÚN `dr-rev/docs/runtime-aica.md:35-36`); con Ikaruga, 86,7 M instrucciones ARM en 30 s ≈ 2,9 MIPS
(SEGÚN `RESULTADO-IKARUGA.md`), más mezclador de 64 canales y DSP (`runtime/src/aica/mixer.cpp`,
CONFIRMADO `ls`).

| Opción | Qué es | Costo / riesgo [INFER] |
|---|---|---|
| A. Emular AICA+ARM7 en el Media Engine | mover `Aica` (ARM7, temporizadores, mezclador) al ME; el SH-4 escribe registros y ARAM en memoria compartida sin caché | Sale del núcleo principal (~10 %+ en x86, más en PSP). ~3 MIPS de ARM a 30–60 ciclos por instrucción interpretada = 90–180 MHz del ME + mezcla. Requiere me-core-mapper; sincronización de IRQ ARM↔SH-4 por memoria compartida; el reloj determinista se pierde |
| B. Traducir estáticamente el driver de sonido | tratar el código ARM que el juego sube a ARAM como el SH-4: recompilarlo a C++ y correrlo (en el ME o en el principal) | 5–10× menos CPU que interpretar. Hay que capturar el binario del driver desde ARAM (`--dump-aram`, CONFIRMADO `boot_main.cpp:1160`, `:2193`) y que no se modifique en tiempo de ejecución. Herramienta nueva (traductor ARM) |
| C. HLE | ignorar el ARM7: interpretar los comandos que el juego manda al driver y mezclar con código propio | Lo más barato en CPU y ahorra los 2 MB de ARAM; exige ingeniería inversa del protocolo del driver. Riesgo alto de diferencias audibles |
| D. Sin audio | `--no-audio` equivalente | Cero. Primer hito |

Idea del ME tomada de sf64 `src/psp/audio_me.c` (GPL, solo descrito): memoria compartida por el
alias sin caché `0x40000000` (`:54`), bandera `READY 0x100` (`:53`), sondeo con
`sceKernelDelayThread` (`:6078`), VME vía `me-core-mapper/vme-lib.h` (`:47`, `:414-424`).
oot usa la misma librería (SEGÚN `ikaruga-psp-estudio/ESTUDIO.md` §2.2; no lo abrí).

Recomendación [INFER]: D → A (reutiliza el código de dream-recomp tal cual, solo cambia dónde
corre) → B si A no alcanza. C solo si la RAM obliga.

## 6. (e) Piezas a construir, en orden

1. **Medición en PC** (sin PSP): cobertura de funciones traducidas en los 5 capítulos, velocidad
   headless en gameplay, modo de FPU usado, tamaño del binario con `-Os`.
2. **Build cruzado**: toolchain pspdev (psp-g++) para el runtime + `gen/`; `size` en MIPS. Decide
   §2 con números reales.
3. **Capa de plataforma mínima**: `main` con 333 MHz, VFPU, heap, `MEMSIZE=1`; `sceIo` para
   `1ST_READ.BIN`, disco extraído y VMU (filas 8–10); reloj (fila 7). Arranque headless en PSP con
   reporte de ciclos → primera cifra de velocidad.
4. **Camino rápido de memoria** inline y verificado contra el reporte de PC (mismos contadores).
5. **Video**: backend sceGu desde `dream::render` (filas 1–4): listas opaca/punch/translúcida,
   texturas 16 bit swizzleadas con caché acotada, TATE/horizontal.
6. **Entrada**: sceCtrl → Maple, con remapeo TATE (fila 5).
7. **Audio**: salida sceAudio (fila 6) con el AICA en el principal para validar; luego AICA al ME.
8. **Recortes de RAM** (§2) y overlays de código si hacen falta.
9. **Empaquetado**: EBOOT.PBP con `mksfoex -d MEMSIZE=1` + `pack-pbp`; instrucciones para que el
   usuario ponga su disco extraído en `ms0:` (nada del juego se distribuye).

## Qué NO verifiqué y dónde está mi punto ciego

- Nada corrió en una PSP ni en PPSSPP. Todo §3 (velocidad), §2 (tamaño del código MIPS) y los
  costos de §5 son inferencias.
- No compilé el runtime para MIPS: no sé si usa construcciones que psp-g++/newlib no soporten
  (excepciones en la ruta caliente, `<filesystem>`, `std::chrono`, el control de redondeo de `runtime/src/fenv.cpp`).
- No revisé si Ikaruga usa precisión doble del SH-4 ni cuántas instrucciones FIPR/FTRV ejecuta.
- No corrí el build release en esta tarea: los números de PC son de `RESULTADO-IKARUGA.md` y de
  `size`/`sha1sum` sobre el árbol ya compilado.
- Los estudios de memoria y CPU de dream-recomp se midieron con Crazy Taxi en macOS; su reparto
  del perfil puede no valer para Ikaruga.
- No confirmé que el modo TATE del juego dibuje rotado en 640×480 ni cómo se activa.
- No abrí el código de audio de oot (cité el estudio) ni la versión VME de sf64 más allá de las
  líneas indicadas; no medí latencias.
- No verifiqué que el driver de sonido del juego no se modifique en ARAM (condición de la opción B).
- De oot solo abrí archivos y líneas puntuales; su arquitectura completa (interpolación de frames,
  gestión de VRAM) la conozco por el estudio, no por lectura propia.
