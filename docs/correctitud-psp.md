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

## Fila 8b — divergencia de 1983 con FPU `double` (denormales y NaN, no la entrada)

Pregunta: con FIPR/FTRV/FMAC en `double` en los dos lados y las pulsaciones del capítulo 1, ¿por qué
PC y PSP escriben distinto desde el cuadro ~1983? Respuesta: **no es la entrada**. Son dos
diferencias de la FPU de la PSP frente a x86 que el build `double` deja ver, y con `double` sí son
errores del port (la igualdad debe ser total).

**Entrada descartada (código).** PC (`dr-idle/runtime/boot/boot_main.cpp`, `on_vblank_out`) y PSP
(`psp/stubs/input.cpp` `input_poll`, llamado desde `on_vblank_out` en `psp/ikaruga/main.cpp`) hacen lo
mismo: en cada VBlank-out, con `f = spg.frames()`, el botón está activo (bajo) si `start <= f < start + 8`,
y después `maple.vblank()`. Mismo cuadro, misma duración, misma encuesta. Además, la divergencia
es una escritura de punto flotante, no del estado del mando, y el hash es igual hasta el cuadro 1982
(con seis pulsaciones ya entregadas, de 700 a 1800).

### Reproducción (paso 1)

EBOOT `DREAM_FPU_DOUBLE=1` sin arreglo contra `ikaruga_boot` de `dr-idle` (double), `PULSAR=` del capítulo 1,
`hash_every=1`:

| | valor |
|---|---|
| último cuadro igual | 1982 (`2e5edbc3b15b7716`, 164 185 556 escrituras en los dos) |
| primer cuadro distinto | **1983** (PC `f8bafff30a62678c`, PSP `11611aa3021f578c`; mismo nº de escrituras, 164 608 083) |
| primera escritura distinta | nº **164 579 168**, `0x8C00EFA0`, 4 bytes: PC `0x00000000`, PSP `0x057DE800`; PC del guest `0x0C06D576` (el `jsr` a `njGetTranslation` 0x0C014740, que guarda fr0..fr2 con `fmov fr,@-r5`), PR `0x0C06D57A` |

Comandos: PC `ikaruga_boot … --press <capítulo 1> --write-log w.log --write-log-range 164185556:422527`;
PSP `ikaruga.cfg` con `log_from=164185556`, `log_count=422527` y `PROBAR_SACAR=writes.log`; `diff` de
los dos logs.

`0x057DE800` es un float normal pequeño (≈1,2e-35); en PC sale 0. Las escrituras desde 0x0C06D576 son
`fmov` (copia de bits): el valor ya venía distinto en el registro y lo produjo aritmética anterior.

### Causa 1: la PSP no hace DAZ (denormales en la *entrada*)

El runtime traduce FPSCR.DN=1 del guest (Katana arranca con 0x00040001) a FTZ+DAZ de MXCSR en x86
(`runtime/src/fenv.cpp`) y a FCSR.FS en la PSP (parche 0001). Línea de sonda en el arranque del
EBOOT (`log_fpu_probe`, `main.cpp`), en PPSSPP:

```
fpu: fs 1 denormal*1e10 0f4ad2fa tiny*tiny 00000000 soft-double in 1e-29 out 00000000
```

Con FS=1 un **resultado** denormal sí se vacía (`tiny*tiny = 0`), pero una **entrada** denormal se usa
tal cual: `1e-39 * 1e10 = 0x0F4AD2FA` (≈1e-29), donde x86 con DAZ da 0. El juego carga denormales de
memoria a registros (`fmov @r,frn`, copia de bits) y luego opera con ellos: en PC valen 0 y en la PSP
no. Además, el `double` de FIPR/FTRV y `fmaf` van por software en la PSP (libgcc) y no miran FCSR.FS:
su conversión final a `float` deja pasar un denormal que x86 (`cvtsd2ss` con FTZ) vacía.

Por eso el intento de la fila 9b (vaciar solo en FIPR/FTRV/FMAC) no cambiaba nada: la entrada denormal
entra por los `fadd`/`fmul`/`fcmp` generados, no solo por esas tres operaciones.
[INFER] Lo medí en PPSSPP; que la Allegrex real se comporte igual no lo verifiqué.

### Causa 2: el NaN del `double` por software

Arreglada la causa 1, la siguiente divergencia apareció en el cuadro **2277** (último igual: 2276):
escritura nº **275 724 434** en `0x8CAF6618`, PC `0xFFFFFFFF`, PSP `0x7FFFFFFF`, PC del guest `0x0C068F86`
(`jsr` a `njPushMatrixEx` 0x0C01429C, que guarda la matriz con `fmov`). Las dos son NaN y difieren en el
signo. [INFER] sale de un FTRV en `double` con un NaN en la matriz o en el vector: x86 propaga el primer
operando NaN (silenciado) o el NaN por defecto `0xFFC00000`, mientras que el `double` por software de
libgcc da otro signo. No aislé la instrucción exacta; el arreglo de abajo la iguala, y eso es la evidencia.

### Arreglo (parche `patches/dream-recomp/0010-soft-daz.patch`)

Actúa solo con `-DDREAM_SOFT_DAZ=1`, que `psp/ikaruga/Makefile` y `scripts/build-psp-ikaruga.sh` encienden por
defecto cuando `DREAM_FPU_DOUBLE=1` (`DREAM_SOFT_DAZ=0` lo apaga para la contraprueba). El build `float`
que se entrega no cambia: ahí queda apagado, porque en `float` una divergencia por FPU no es un bug.

- `ctx.h`: `fr[]`/`xf[]` pasan a `Freg`, un `float` envuelto. Al leerse como `float` para operar, un
  denormal vale ±0 (DAZ); `=`, `fmov` y `f2u` guardan los bits crudos (x86 tampoco toca los bits en una copia).
- `ops.h`, rama `double`: FIPR, FTRV y FMAC vacían un resultado `float` denormal (FTZ) y, si el
  resultado es NaN, lo recalculan con la regla de x86 (`x86fp::dot4`, `x86fp::fma`).
- `build-psp-ikaruga.sh`: los objetos generados se recompilan cuando cambian `DREAM_FPU_DOUBLE`,
  `DREAM_SOFT_DAZ` o algún parche. Esos objetos incluyen `ops.h` en línea y make no rastrea las cabeceras;
  sin esto, el primer intento salió con el mismo md5 que el anterior.
- `main.cpp`: la línea `fpu:` de la sonda, en todos los builds.

### Aceptación (paso 3) — queda en el repo

```
G=/home/unjordi/code/ajenos/dr-idle/build-rel/games/ikaruga/gen
J=/home/unjordi/code/ajenos/dr-idle/games/ikaruga/extracted
export PROBAR_DISCO=…/DISCO-real.ISO DREAM_RECOMP=/home/unjordi/code/ajenos/dr-idle
DREAM_FPU_DOUBLE=1 scripts/build-psp-ikaruga.sh $G --variantes=mix                    # arreglo
scripts/test-psp-igual-pc-double.sh build/psp/ikaruga/EBOOT.PBP $J
DREAM_FPU_DOUBLE=1 DREAM_SOFT_DAZ=0 scripts/build-psp-ikaruga.sh $G --variantes=mix   # revertido
scripts/test-psp-igual-pc-double.sh --contraprueba build/psp/ikaruga/EBOOT.PBP $J
```

`test-psp-igual-pc-double.sh` corre `test-psp-igual-pc.sh` con el `PULSAR=` del capítulo 1, `HASTA=2600`
y `hash_every=1`, y además compara **todos** los cuadros que registraron los dos lados. La PSP necesita
unos 1 500 s de PPSSPP para llegar al cuadro 2600.

| prueba (2026-11-11) | EBOOT md5 | resultado |
|---|---|---|
| sin arreglo (paso 1) | `0d778aaa…` | primer cuadro distinto 1983 |
| solo DAZ/FTZ (sin la regla de NaN) | `da2d7b9e…` | iguales 1…2276, distinto en **2277** |
| **arreglo completo** | `f760c959…` | **PASA**: los 43 cuadros 60…2600 iguales |
| contraprueba `DREAM_SOFT_DAZ=0` | `6c4221aa…` | FALLA como debe: primer cuadro distinto **1983** (`test-psp-igual-pc.sh` cada 60 falla en 2040); la contraprueba del script pasa |

Arranque rápido con el build `float` que se entrega (`DREAM_SOFT_DAZ` apagado, md5 `4b2d8f8b…`, con la
línea `fpu:` nueva): `HASTA=240 test-psp-igual-pc.sh` → iguales los 4 cuadros 60…240.

Evidencia (fuera del repo, en `build/f8b/`): `pcl/w.log` y `pspl/writes.log` (ventana 164 185 556 + 422 527),
`pcl2/` y `pspl2/` (ventana 275 455 580 + 299 836), `r3/` (aceptación) y `contra/`.

### Qué NO verifiqué en la fila 8b y dónde está mi punto ciego

- **Solo PPSSPP.** La sonda `fpu:` (sin DAZ en las entradas, con FTZ en los resultados) es lo que hace PPSSPP;
  no la corrí en una PSP real. Si la Allegrex real sí hiciera DAZ, el parche sobraría ahí, pero no estorbaría.
- **Causa 2 inferida.** No aislé qué instrucción produjo el NaN de signo distinto; supongo que fue FTRV o FIPR
  en `double`, porque solo ahí puse la regla de NaN de x86 y con eso la escritura se iguala. Los `fadd`/`fmul`
  generados en `float` usan la FPU de hardware de la PSP, y su NaN no lo comparé con el de x86.
- **Alcance.** Igualdad total probada solo con las pulsaciones del capítulo 1 hasta el cuadro 2600 (todos los
  cuadros). Más allá, o con otra entrada, puede aparecer otra diferencia de FPU.
- **Costo.** No medí cuánto frena `Freg` el build `double`; solo sirve para depurar. El build `float` compila
  `Freg = float` (sin cambio de código), pero su EBOOT cambia de md5 por la sonda de `main.cpp`. Su velocidad
  la volví a medir (juego 2600 0,0869x, título 1243 0,1458x; tabla de la fila 8b en
  docs/velocidad-psp.md, una corrida cada uno); `--todas` no lo corrí: le toca al orquestador.
- `devinterp/interpreter.cpp` toma `float&` de `c.fr[n]`: con `DREAM_SOFT_DAZ=1` no compilaría. En la PSP no
  se compila, y el lado de PC no lleva este parche.
- La entrada la descarté leyendo el código de los dos lados y viendo que la primera diferencia es de punto
  flotante. No registré el estado del mando cuadro por cuadro en los dos lados.

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
