> Foto de las mediciones del 2026-11-05; el estado vigente está en [`../plan.md`](../plan.md) y en el índice [`../README.md`](../README.md).

# Fila 0: comparación de recompiladores estáticos de Dreamcast

Fecha: 2026-11-05. Ejecutable de prueba: `1ST_READ.BIN` de Ikaruga (Japón, T38706M),
sha1 `f2524eb571da1b0fb4a9b2cdab448ce24764c6f9`, carga en 0x8C010000. Aquí no se incluye nada del
disco, del C generado ni capturas.

## Revisión (2026-11-05 22:10)

Cada afirmación del agente que la fila usa, repetida contra la fuente:

| Afirmación | Veredicto | Cómo |
|---|---|---|
| sha1 de `1ST_READ.BIN` `f2524eb5…`, carga 0x8C010000, T38706M área J | CONFIRMADO | `sha1sum`; IP.BIN |
| dcrecomp: 0 STARTRENDER, 3 presents en 40 s, no determinista (bucle 0x8C044FA8/0x8C0451A0 o tormenta de IRQ en pc=0x8C048A94) | CONFIRMADO | `proj/run.sh 40` dos veces: una corrida dio la tormenta (reent≈4.8 M), la otra el bucle |
| dcrecomp: el ARM7 corre el driver de sonido | CONFIRMADO que el ARM7 corre (contador `arm7=` sube); que sea MANATEE.DRV: SEGÚN agente (el nombre no sale en el log) | log de la corrida |
| dream-recomp trae `games/ikaruga/` con 1 717 símbolos del SDK | CONFIRMADO | `symbols.tsv`: 1 720 líneas, 3 de cabecera |
| dream-recomp traduce las store queues por UTLB y cita Ikaruga | CONFIRMADO | `docs/runtime-memory.md:34-39`; es el mismo bug que nuestro parche 3 de dcrecomp |
| dream-recomp "pide la boot ROM" | CORREGIDO: es opcional. Sin ella los mensajes de la tarjeta de memoria del título salen en blanco (`games/ikaruga/README.md`); el README principal dice que no hace falta imagen de BIOS | README |
| dream-recomp: "boots, renders, saves" con Ikaruga | CONFIRMADO | Reconstruido desde cero en un worktree limpio con los comandos de la medición (rama `axon/mide-dream-recomp-con-ikaruga-fi-p4rail` de nuestro clon, commit `a0fa969` sobre `51ca276`): build release sin intérprete, binario de 14 425 888 B. Con ventana y START/A simulados: capítulo 1 con texturas a 640×480, HUD, nave y escenario iguales a la versión de Steam. Headless: 60 s de juego en 11.7 s de host (5.1× tiempo real, incluye gameplay), 3 313 renders, `untranslated call targets: 0` |
| Licencias: dcrecomp GPL-2.0, dream-recomp GPL-2.0, DreamcastRecompiled MIT, dreamcomp GPL-2.0 (estaba "?"), SARecomp sin licencia | CONFIRMADO | `gh api repos/<r>` |
| KatanaRecomp da 404 | CONFIRMADO | `gh api repos/sonicfreak1337/KatanaRecomp` |
| Último commit de dream-recomp | `51ca276`, 2026-10-29 | `git log -1` |
| "24 MB de RAM útil en PSP-2000+ (32 MB en total)" | CORREGIDO: PSP-1000 = 32 MB (≈24 útiles); PSP-2000 en adelante = 64 MB (≈52 útiles, con `MEMSIZE=1` en el PARAM.SFO, como hacen sf64-psp y oot-psp) | `Makefile:647` de sf64-psp, `Makefile:1852` de oot-psp |
| Comandos de "Para retomar" | CORREGIDO: el disco se extrae con `dcdisc extract` a `games/ikaruga/extracted`; el lanzador corre sin `--bios`; ver `games/ikaruga/README.md` | README |

La decisión de abajo descansa en lo CONFIRMADO de esta tabla.

Etiquetas del agente: **[MEDIDO]** lo corrí aquí. **[DOC]** lo dice el README o la documentación del
proyecto, pero no lo comprobé. **[INFER]** es una deducción mía, sin medir.

> **Límite de esta corrida:** el sandbox del agente bloqueó cualquier escritura fuera del worktree
> (`~/code/ajenos/ikaruga-fw/` y los directorios `build/` de los clones). Por eso **solo dcrecomp
> tiene medición con Ikaruga**, de la fase 0 anterior (`~/code/ajenos/ikaruga-dc/NOTAS.md`). Los
> demás frameworks están clonados o revisados, pero no se compilaron ni se corrieron. Los comandos
> para medirlos están al final.

## Candidatos encontrados

Búsqueda: `gh search repos` con "dreamcast recompiler", "sh4 recomp", "naomi recompiler",
"KatanaRecomp" y "dream-recomp".

| Proyecto | Qué es |
|---|---|
| [sp00nznet/dcrecomp](https://github.com/sp00nznet/dcrecomp) | Recompilador SH-4 → C, más un runtime para Dreamcast/NAOMI |
| KatanaRecomp (sonicfreak1337) | El README de dcrecomp lo cita como "further along": SH-4 → C++ a través de una IR. **El repo da 404** (`gh repo view sonicfreak1337/KatanaRecomp`): es privado o lo borraron. Solo se ve su uso en [SARecomp](https://github.com/sonicfreak1337/SARecomp) (Sonic Adventure) |
| [phobos665/dream-recomp](https://github.com/phobos665/dream-recomp) | Recompilador más runtime. **Ya trae `games/ikaruga/`** con `ikaruga.toml` y `symbols.tsv` (1 717 funciones del SDK Katana identificadas) |
| [danielgomesvieira2000/dreamcomp](https://github.com/danielgomesvieira2000/dreamcomp) | Framework "built on dream-recomp" (derivado). Lo usa soulcalibur-recomp |
| [Duw0ng/DreamcastRecompiled](https://github.com/Duw0ng/DreamcastRecompiled) | "Experimental", recompilación SH-4 más emulación selectiva de hardware. Enfocado en Crazy Taxi 2 y un tester de AICA |
| Otros (no son frameworks) | tyrannotorus/emulator-dreamcast-deadcast (emulador "con soporte de recomp"), DCRecomp/BlueStingerRecomp (C++20, Windows x64), y ports de un solo juego (Street Fighter Alpha 3, Rez) |

## Tabla comparativa

| | dcrecomp | dream-recomp | dreamcomp | DreamcastRecompiled | KatanaRecomp |
|---|---|---|---|---|---|
| **Hasta dónde llega Ikaruga** | **[MEDIDO]** Compila (15 995 funciones, 27 MB de C) y arranca, pero solo con 3 parches locales (GD GetCmdStat, TOC GD, store queues con UTLB). Inicializa el GD-ROM, carga MANATEE.DRV en el AICA, sube una textura por DMA de CH2 y encuesta Maple. **No dibuja ningún cuadro**: 0 STARTRENDER y 3 presents negros en 40 s. No es determinista: unas veces queda en un bucle en 0x8C044FA8/0x8C0451A0 y otras en una tormenta de IRQ en pc=0x8C048A94 | No medido. **[DOC]** Tiene configuración propia de Ikaruga (load 0x8C010000 / link 0x0C010000, handlers de VBR, semillas de descubrimiento). Su README dice que el título dibuja los mensajes de la tarjeta de memoria con la fuente de la boot ROM, y `runtime-render.md` habla del "first stage" de Ikaruga. **[INFER]** Eso apunta a que llega a dibujar al menos el título | No medido | No medido | No disponible (404) |
| **Texturas PVR2 / TA** | Parcial: el TA recibe y la textura se sube, pero no hay render [MEDIDO] | [DOC] Hay un `runtime-render.md` dedicado, que describe la niebla y las listas de pantalla de Ikaruga | [INFER] como dream-recomp | [DOC] Selectiva | ? |
| **AICA / sonido** | ARM7 interpretado, corre MANATEE.DRV [MEDIDO] | [DOC] Registros AICA mapeados; el plan menciona streaming ADX | [INFER] como dream-recomp | [DOC] Tiene un "Aqua AICA tester" | [DOC SARecomp] reproduce películas |
| **MMU / store queues** | SQ sin traducción UTLB: hizo falta el parche 3 [MEDIDO] | [DOC] SQ traducida por UTLB con MMUCR.AT, páginas de 1 KB a 1 MB. Cita Ikaruga explícitamente (`runtime-memory.md:34-39`) | [INFER] heredado | ? | ? |
| **GD-ROM** | Vía BIOS HLE; GetCmdStat/TOC necesitaron parche y falta `cmd 40` [MEDIDO] | [DOC] Lee el `.chd` directamente (`dcdisc`). Pide la boot ROM real (`--bios`) para la fuente del sistema | ? | ? | ? |
| **Interrupciones** | Holly IML2/4/6 hacia el despachador del juego (0x8C042F26), conectado a mano en main.c [MEDIDO] | [DOC] Los 4 handlers de excepción/IRQ se declaran en el toml; TMU/INTC/DMAC en sus paquetes de trabajo | ? | ? | ? |
| **Último commit** | `3c60e6f` 2026-10-20 | Clonado en `~/code/ajenos/dream-recomp` (fecha: `git -C … log -1`) | push 2026-11-04 | `acdaaac` 2026-10-18 | SARecomp: push 2026-11-05 |
| **Licencia** | GPL-2.0 | GPL-2.0 (`LICENSE`) | ? (no revisada) | MIT | ? |
| **¿C portable a MIPS/PSP?** | [INFER] Sí en principio: es C plano, con 7.3 MB de text en x86-64, contra ≈24 MB útiles en PSP-1000 o ≈52 MB en PSP-2000 en adelante (ver revisión). Hay que medir el tamaño en MIPS | [INFER] Probable. Revisar dependencias de host (SDL/GL) y el tamaño | [INFER] como dream-recomp | [INFER] Dudoso: se enfoca en PC y mezcla emulación selectiva | No: genera C++ y el código no está disponible |

## Decisión

**Seguir con dream-recomp (phobos665).** Medido: con nuestro disco llega al capítulo 1 con texturas, sonido y guardado, sin intérprete. dcrecomp, con 3 parches, no dibuja ningún cuadro. dcrecomp queda solo como línea base medida.

Por qué:
1. Es el único que ya tiene a Ikaruga como proyecto configurado (toml, símbolos del SDK, handlers
   de VBR).
2. Su documentación resuelve, de forma explícita y para Ikaruga, el bug que nos bloqueó en
   dcrecomp: store queues traducidas por la UTLB.
3. Tiene la misma licencia GPL-2.0, así que no cambia nada de lo que dice el README sobre
   licencias.
4. Tiene ecosistema: dreamcomp y soulcalibur-recomp se construyen sobre él.

KatanaRecomp no se puede usar mientras no sea público. DreamcastRecompiled es MIT, pero está
centrado en otros juegos y es experimental.

Criterio para revertir: si dream-recomp no saca un cuadro de Ikaruga en PC, o si su runtime no se
puede separar del host PC, se vuelve a dcrecomp con los 3 parches y se ataca el bucle de espera.

## Qué NO verifiqué

- **No compilé ni corrí dream-recomp, dreamcomp ni DreamcastRecompiled.** El sandbox bloqueó
  escribir fuera del worktree. No hay log, dirección ni captura de esos tres.
- Que dream-recomp llegue al título o a la primera fase con Ikaruga: lo dice su documentación, no
  una medición mía.
- El contenido de KatanaRecomp (repo 404) y la licencia de SARecomp y de dreamcomp.
- La fecha exacta del último commit de dream-recomp.
- Que el runtime de dream-recomp funcione sin la boot ROM. Su README dice que sin ella los
  mensajes salen en blanco.
- Tamaño del código ni rendimiento en MIPS: ningún framework se compiló con el toolchain de PSP.
- Si existe algún recompilador específico de NAOMI fuera de GitHub (foros, GitLab).

### Para retomar la medición (fuera del sandbox)

```bash
mkdir -p ~/code/ajenos/ikaruga-fw && cp ~/code/ajenos/ikaruga-dc/disc_extract/1ST_READ.BIN ~/code/ajenos/ikaruga-fw/
cd ~/code/ajenos/dream-recomp && git log -1 --format='%h %cI'
# poner el disco/extraído en games/ikaruga/{disc,extracted} (gitignored) según games/ikaruga/README.md
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target ikaruga_boot --parallel 32
XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=$(ls /run/user/1000 | grep -m1 '^wayland-[0-9]*$') \
  timeout 40 build/games/ikaruga/ikaruga_boot --config games/ikaruga/ikaruga.toml --bios dc_bios.bin --window \
  > ~/code/ajenos/ikaruga-fw/dream-recomp.log 2>&1
```
