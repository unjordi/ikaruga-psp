# Estado guardado

Para qué: una prueba que mira el cuadro 2600 ya no tiene que emular desde el arranque. Se vuelca el
estado completo de la máquina en el cuadro N y el EBOOT arranca desde ahí,
con el mismo resultado que una corrida desde el arranque.

## Cómo se usa

**PSP** (`ikaruga.cfg` junto al EBOOT):

| clave | efecto |
|---|---|
| `guardar_estado_en=N` | vuelca `estado-N.bin` (≈27,6 MB) junto al EBOOT la primera vez que el guest está en la espera de vblank de un cuadro ≥ N. El juego sigue corriendo. |
| `cargar_estado=archivo` | arranca desde ese archivo (ruta relativa a la carpeta del EBOOT) en vez de desde el arranque. |

Con PPSSPP: `PROBAR_SACAR="estado-600.bin"` trae el volcado; `PROBAR_METER="ruta/estado-600.bin"`
(de `scripts/probar-ikaruga-ppsspp.sh`) lo copia a la carpeta del EBOOT antes de correr.

```sh
P="start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200"
printf 'rtc_semilla = 1\npulsar=%s\nhash_every=1\ncapturar_en=2600\nmax_cuadros=2601\nguardar_estado_en=2590\n' "$P" >a.cfg
PROBAR_SIN_LIMITE=1 PROBAR_CFG=a.cfg PROBAR_SACAR=estado-2590.bin \
  scripts/probar-ikaruga-ppsspp.sh build/psp/ikaruga/EBOOT.PBP $J 1500 a/psp.log
printf 'rtc_semilla = 1\npulsar=%s\nhash_every=1\ncapturar_en=2600\nmax_cuadros=2601\ncargar_estado=estado-2590.bin\n' "$P" >b.cfg
PROBAR_SIN_LIMITE=1 PROBAR_CFG=b.cfg PROBAR_METER=a/estado-2590.bin \
  scripts/probar-ikaruga-ppsspp.sh build/psp/ikaruga/EBOOT.PBP $J 600 b/psp.log
```

El cfg de la carga tiene que repetir lo que afecta al guest después del cuadro N: las mismas
`pulsar=` (el guion se aplica por número de cuadro, y el contador de cuadros viene en el estado) y el
mismo `rtc_semilla` (el reloj del AICA sí va en el estado; la semilla solo importa al arrancar). Las
claves de depuración del host (`hash_cada`, `capturar_en`, `dibujar_desde`…) son libres.

## Punto de volcado

`System::before_idle` se llama al principio de `System::on_idle`, es decir en cada
`idle_poll` del código generado. El host vuelca cuando `pc == 0x0C0370BA` (cabeza de la espera de
`kmWaitVBlank`, `docs/mediciones/investigacion-pendientes.md` §23), `spg.frames() >= N`, `nesting == 0` y
`c.cycles < c.next_event`. La última condición: si ya hay una interrupción vencida, la
función reanudada (`kmWaitVBlank__resume`) la atendería en su sondeo de entrada, antes que el
sondeo donde la tomó la corrida original; esa vuelta se salta y se vuelca en la siguiente.

Al cargar, `run_guest_from(c, m, 0x0C0370BA, stop_pr)` (`abi_bare.cpp`) entra al bucle de
`run_guest_impl` ya reanudado: `resume_at` llama a `kmWaitVBlank__resume` con ese PC y, cada vez
que una función reanudada vuelve, sigue en `c.pr`, que cada función restaura de la pila del guest
(en la RAM volcada). `stop_pr` es el `PR` con que arranca `run_guest` normal (el de `setup_boot`).

## Formato

Un archivo binario de secciones `tag(4) | versión(u32) | tamaño del cuerpo(u32) | cuerpo`, en el
orden del host (little-endian; solo lo carga el mismo build en el mismo tipo de host).
`state::Reader` falla con el nombre de la sección si el tag, la versión o el tamaño leído no
coinciden. Código: `runtime/include/dream/runtime/state.h`, `runtime/src/state.cpp` y
`Mixer::save/load` en `aica/mixer.cpp` (parche `patches/dream-recomp/0011-saved-state.patch`).

| sección | qué lleva |
|---|---|
| `HOST` | PC de reanudación, cuadro |
| `SYS_` | `Ctx` completo, el turno anterior del idle-skip (`idle_prev`), contadores de idle/interrupciones/traps/accesos a dispositivo |
| `SCHD` | reloj `now_` y, por evento en orden de registro, **nombre** y plazo. Los callbacks no se guardan: el host registra los mismos dispositivos en el mismo orden y `load` comprueba cada nombre |
| `INTC` `TMU_` `HINT` `SPG_` | INTC SH-4, TMU, INTC de Holly, SPG (cuadros, línea, vsync) |
| `MEM_` `UTLB` | RAM 16 MB, VRAM 8 MB, ARAM 2 MB, flash 128 KB, OCRAM 8 KB, épocas de VRAM (parche 0007), `write_hash` y su contador, registros CCN/SQ/QACR, UTLB. La ROM de arranque no (el BIOS HLE no la usa) |
| `PVR_` `TA__` | registros de Holly, FIFO del TA a medio llenar, YUV, el estado del TA y su `stream` del cuadro en curso |
| `DMAC` `SYSB` `G2DM` | DMAC SH-4, bloque de sistema, G2-DMA |
| `REGF` ×8 | los `RegisterFile` del host (G1, PVR-DMA, disparadores DMA, BSC, UBC, CPG, RTC SH-4, SCIF en PSP) |
| `MAPL` | registros de Maple, tramas pendientes, mando (botones) y la imagen de la VMU en memoria (el archivo `VMU.BIN` no se reescribe) |
| `AICA` `ARM7` `MIXR` | registros del AICA (32 KB), timers, ARM7 completo, DSP y los 64 canales del mezclador. Los punteros del canal se rehacen al cargar (registro del canal, ranura DSP y fila de `kPlfoScales` como índices, funciones de paso desde los registros) |
| `RTC_` | segundos del RTC del AICA |
| `BIOS` | GD-ROM HLE: comando en curso, sector, restantes, destino. El disco no se guarda: el host lo reabre y lo vuelve a enganchar |

No se guardan: contadores de diagnóstico del host, caché de texturas de la PSP (se rehace al
dibujar; las épocas de VRAM sí van), estado del vídeo del host.

## Cómo se comprueba

Con las pulsaciones del capítulo 1 (las de `test-psp-igual-pc-double.sh`) y `rtc_semilla 1`, en PPSSPP sin
limitador (`DISCO-real.ISO`), el criterio es que la corrida desde el estado dé lo mismo que la corrida
desde el arranque:

- las líneas `hash` de los cuadros posteriores al volcado son idénticas (`hash_cada=1`);
- `captura-2600.raw` es idéntica (`cmp`);
- volcar no altera la corrida: 0→2600 con y sin `guardar_estado_en` dan los mismos `hash`.

Cubierto en los builds `double` (volcado en 600, carga 600→2600) y `float` (volcado en 2590, carga
2590→2600, ≈8 s de pared). La contraprueba es cargar un volcado con el plazo de `aica.sample`
movido +1 000 ciclos: los `hash` difieren desde el primer cuadro cargado. Cargar cuesta ≈2,9 s dentro
del EBOOT (leer 27,6 MB por el sistema de archivos de PPSSPP); volcar, 0,30 s. El arranque normal sin
estado sigue igual al PC (`test-psp-igual-pc-double.sh`).

## Qué no cubre

- Solo un punto de volcado en dos cuadros (600 en el título/menú, 2590 en el capítulo 1). No se probó
  volcar con un render en vuelo distinto, con un comando de GD-ROM en curso ni con DMA de G2
  pendiente; los campos están en el estado pero ninguna prueba los ejerce con valores no triviales.
- El orden de registro de los eventos del scheduler se comprueba por nombre; si un host registra
  un evento más, la carga falla en vez de adaptarse.
- La VMU: el estado restaura la imagen en memoria, no el archivo. Si entre el volcado y la carga
  cambia `VMU.BIN`, la siguiente escritura del juego mezcla los dos.
- No se midió el estado en hardware real ni con limitador.
