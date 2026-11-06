# Fila 8 — correctitud en la PSP

> Todo se midió en **PPSSPP 1.20.4 (flatpak)**, no en una PSP real. La suspensión y HOME se
> provocan desde el propio EBOOT con claves de depuración de `ikaruga.cfg` (abajo), no con la
> consola.

Variables de los comandos:

```sh
export DREAM_RECOMP=<dream-recomp checkout con build-rel/>
G=$DREAM_RECOMP/games/ikaruga/extracted
E=build/psp/ikaruga/EBOOT.PBP
DREAM_RECOMP=$DREAM_RECOMP scripts/build-psp-ikaruga.sh $DREAM_RECOMP/build-rel/games/ikaruga/gen --solo-os
```

EBOOT final: sha256 `a74682950c034f04f51753f75dbbc20a028048cb52eb8358a69bf0c4cf9bbaa4`.
«Antes» es el EBOOT de `develop` (`e7ce644`) hecho con el mismo comando, salvo en 1 (el programa
de prueba con el parche viejo) y en 5 (el EBOOT con callbacks pero sin reabrir el disco).

Revisión (2026-11-06 14:0x): integrada con develop (idle-skip) y repetidas las siete pruebas desde cero: todas pasan; `write_hash` igual PC↔PSP en los cuadros 60…600 con idle-skip en ambos lados (CONFIRMADO).

## Disco: el oráculo necesita el disco real

El `DISCO.ISO` que arma `scripts/preparar-disco-ikaruga.sh` (genisoimage sobre `fs/`) **no** es el
disco: los archivos quedan en otros sectores y el juego lee otros datos. Con él, el hash de
escrituras se separa del de PC en el **cuadro 4** (escritura nº 373 339, `0x8C134ED4`: `0x410861B4`
en PC y `0x4100F978` en PSP). Para comparar con PC hace falta el área HD del disco real, volcada
del CHD con `tools/volcar-area-hd.cpp`:

```sh
D=$DREAM_RECOMP; L=$(find $D/build-rel/third_party -name '*.a' | tr '\n' ' ')
g++ -std=gnu++20 -O2 -I$D/runtime/include tools/volcar-area-hd.cpp \
  $D/build-rel/runtime/libdream_runtime.a $L $L -lzstd -o build/tools/volcar-area-hd
build/tools/volcar-area-hd "$D/games/ikaruga/disc/Ikaruga (Japan).chd" build/DISCO-real.ISO
export PROBAR_DISCO=$PWD/build/DISCO-real.ISO   # lo usan probar-ikaruga-ppsspp.sh y los tests
```

Resultado: 504 150 sectores (pista 3 desde LBA 45000), 1 032 499 200 bytes, ninguno ilegible.
Ni el CHD ni el volcado van al repo.

## Arreglos

| # | Qué | Prueba | Antes | Después |
|---|---|---|---|---|
| 1 | Redondeo: el parche `0001` escribe y lee FCSR.RM (bits 1:0) con `cfc1`/`ctc1` en vez de `fesetround` (stub en newlib-psp) | `scripts/test-psp-redondeo.sh` | `FAIL: toward zero: 1/3 truncates (0x3eaaaaab)` | 5/5 PASS, 1/3 = `0x3eaaaaaa` a cero y `0x3eaaaaab` al más cercano |
| 2 | RTC: semilla `rtc_seed` de `ikaruga.cfg` (default 1, como `--rtc-seed`), y un tick por segundo de guest (200 M ciclos) como el lanzador de PC; antes era `time()` y nunca avanzaba | `scripts/test-psp-rtc.sh $E $G 25` (semilla 1000) | FAIL: no hay lectura del RTC en el log (el EBOOT viejo no la escribe) | RTC en el cuadro 60 = 631 153 001 = 1000 + 631 152 000 + 1 |
| 3 | Oráculo: línea `hash <cuadro> <ciclos> <write_hash> <escrituras> rtc <s>` cada 60 cuadros, muestreada como `--write-hash` de PC (primera línea de barrido de cada cuadro) | `scripts/test-psp-igual-pc.sh $E $G 330` | con el disco real y la VMU de antes: iguales 60–180, **distinto en 240** | **iguales los 10 cuadros 60…600** |
| 4 | HOME: hilo de callbacks con `sceKernelRegisterExitCallback`; el hilo principal sale de `run_guest` entre cuadros y llama `sceKernelExitGame` | `scripts/test-psp-salida-suspension.sh $E $G` (`test_exit_at = 120`) | el guest sigue hasta el cuadro 240 | `stop: exit requested frames 121`, nada después |
| 5 | Suspender: callback de energía (`scePowerRegisterCallback`); en `RESUME_COMPLETE` sube un contador y el disco reabre su imagen por ruta antes de la siguiente lectura | mismo script (`test_suspend_at = 30`: eventos de energía y el descriptor del disco cerrado entre ellos) | sin reabrir: hash distinto desde el cuadro 60 (`c7e833a3…` vs `b003d78f…`) | iguales a PC en 60…240 |
| 6 | Directorio del juego: sale de `argv[0]` | `scripts/test-psp-carpeta.sh $E $G 25` (instala en `ms0:/PSP/GAME/OTRA_CARPETA/`) | FAIL: no escribe nada ahí | corre: `tick frames 60 … pc 0x0c051028` |
| 7 | VMU: parche `0005` (`MemoryCard::writer`, `set_image`); `psp/stubs/vmu.cpp` lee `VMU.BIN` con `sceIo` y cada escritura va a `VMU.BIN.TMP`, `sceIoRename` sobre `VMU.BIN` y `sceIoSync` | `scripts/test-psp-vmu.sh $E $G 100` (dos corridas) | FAIL: la corrida 1 no deja `VMU.BIN` | corrida 1 deja 131 072 B (md5 `a85496b0…`), la 2 la lee y la deja igual |

### Cómo se encontró que faltaba la VMU (arreglo 7 → 3)

Con el disco real, `hash_every = 1` en `ikaruga.cfg` dio el primer cuadro distinto: **206**
(PC `1a41ad4b…`, PSP `4cb9541d…`). Con `log_from`/`log_count` el EBOOT escribe `writes.log` en el
formato de `--write-log` de PC; la primera escritura distinta es la nº 28 370 289 en `0x0C1484C0`:
`0x00010007` en PC y `0x000100FB` en PSP. `0xFB` es `kFileError` de Maple: en la PSP `save()` no
tenía ruta y cada escritura de bloque fallaba. Tras el arreglo 7 el hash queda igual hasta 600.

```sh
printf 'rtc_seed = 1\nhash_every = 1\n' > build/h1.cfg
PROBAR_CFG=build/h1.cfg scripts/probar-ikaruga-ppsspp.sh $E $G 90 build/psp/evidencia/hash1.log
printf 'rtc_seed = 1\nlog_from = 28350887\nlog_count = 422726\n' > build/div/psp206.cfg
PROBAR_SACAR=writes.log PROBAR_CFG=build/div/psp206.cfg scripts/probar-ikaruga-ppsspp.sh $E $G 90 build/div/psp206.log
```

### Regresión

`scripts/test-psp-arranque.sh $E $G 30` sigue pasando (cuadro 60, ciclos 201 989 964).

## `ikaruga.cfg`

Junto al EBOOT; líneas `clave = valor`, `#` comenta. `rtc_seed` (default 1), `hash_every`
(default 60), `log_from`/`log_count` (registro de escrituras), `test_exit_at` y `test_suspend_at`
(solo pruebas).

## Evidencia

La evidencia quedó fuera del repo
(solo entraron `redondeo-antes.log` y `redondeo-despues.log`). El resto está en
`build/psp/evidencia/` del worktree: `final/` (corrida final de todo), `*-antes.txt`,
`*-despues.txt`, `igual-pc/`, `vmu/`, `salida-suspension/`.

## Qué NO verifiqué y dónde está mi punto ciego

- **Nada en hardware.** Que el Allegrex real respete FCSR.RM (y FS) solo lo dice PPSSPP.
- **Suspensión simulada:** cierro el descriptor del disco y mando los eventos yo mismo. Una
  suspensión real (con el reloj, el audio y la VMU de por medio) no la probé; tampoco que el
  kernel entregue `RESUME_COMPLETE` antes de la siguiente lectura.
- **HOME simulado** con `sceKernelNotifyCallback`; no abrí el menú HOME de verdad. PPSSPP se queda
  abierto tras `sceKernelExitGame`, así que el test mira el log, no que la app terminara. El
  respaldo de 3 s del callback (si el guest no llega a un cuadro) no está probado.
- **El oráculo exige el disco real** (`PROBAR_DISCO`); con `preparar-disco-ikaruga.sh` diverge en el
  cuadro 4. No arreglé ese script.
- Iguales de 60 a 600, sin entrada, con semilla 1 y sin VMU previa. Más allá, con entrada o con
  otra VMU, no lo comparé. Tampoco comparé la imagen de VMU de la PSP con la de PC en el mismo cuadro.
- **VMU:** no probé un corte de energía; entre `sceIoRemove` y `sceIoRename` hay un momento en que
  solo existe `VMU.BIN.TMP`, y al arrancar no la recupero. Cada escritura de bloque reescribe
  128 KB (144 escrituras al arrancar): el costo en Memory Stick real no lo medí.
- El submódulo estaba vacío en el worktree: compilé contra otro checkout de dream-recomp (`DREAM_RECOMP`)
  sin poder confirmar con git que esté en `51ca276`.
- `main.cpp`: lo nuevo está en funciones aparte, pero el callback de VBlank y el arranque reciben
  llamadas nuevas (`test_hooks`, `check_exit`, `power_init`). Si la fila idle-skip toca esas
  líneas, el merge puede tener conflicto.
