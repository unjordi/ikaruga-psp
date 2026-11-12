# Correctitud en la PSP

Qué garantiza el EBOOT frente al juego original y qué prueba cada garantía. Las mediciones con las
que se encontró y arregló cada punto están en `docs/mediciones/correctitud-psp.md`.

La imagen se juzga contra Flycast (`docs/referencia-flycast.md`). Contra el PC recompilado solo se
compara el hash de las escrituras del guest (`write_hash`), que caza errores del port y no compara
imágenes. Esa igualdad bit a bit se exige únicamente con el build `DREAM_FPU_DOUBLE=1`; en el build
`float` que se entrega, una diferencia PC↔PSP causada por la FPU de cada CPU (denormales, FTZ/DAZ)
no es un error.

Variables de los comandos: `DREAM_RECOMP` = checkout de dream-recomp con `build-rel/` (receta en
`docs/reproducir-pc.md`), `J=$DREAM_RECOMP/games/ikaruga/extracted`,
`E=build/psp/ikaruga/EBOOT.PBP`, `PROBAR_DISCO` = el área HD volcada (`docs/primer-uso.md`).
Todo se prueba en PPSSPP; ninguna prueba corrió en una PSP real.

## El disco para el oráculo

El oráculo necesita el área HD real del disco (`tools/volcar-area-hd.cpp`; comando en
`docs/primer-uso.md`, paso 7). El ISO de `scripts/preparar-disco-ikaruga.sh` pone los archivos en
otros sectores y el hash de escrituras se separa del PC en el cuadro 4.

## Garantías y pruebas

| garantía | cómo | prueba |
|---|---|---|
| Redondeo del guest: el parche `0001` lee y escribe `FCSR.RM` con `cfc1`/`ctc1` (el `fesetround` de newlib-psp es un stub) | 1/3 trunca a cero (`0x3eaaaaaa`) y redondea al más cercano (`0x3eaaaaab`) | `scripts/test-psp-redondeo.sh` |
| RTC: `rtc_seed` de `ikaruga.cfg` (por omisión 1) y un tick por segundo de guest (200 M ciclos), como el lanzador de PC | con semilla 1000 el RTC del cuadro 60 vale 631 153 001 | `scripts/test-psp-rtc.sh $E $J 25` |
| Mismas escrituras que PC: línea `hash <cuadro> <ciclos> <write_hash> <escrituras> rtc <s>` cada `hash_every` cuadros (60 por omisión) | `write_hash` igual en los cuadros 60…`HASTA` | `scripts/test-psp-igual-pc.sh $E $J` (`HASTA=600` por omisión, `HASTA=240` es el arranque rápido) |
| HOME: hilo de callbacks con `sceKernelRegisterExitCallback`; el hilo principal sale entre cuadros y llama `sceKernelExitGame` | con `test_exit_at=120` el log termina en `stop: exit requested` en el cuadro 121 | `scripts/test-psp-salida-suspension.sh $E $J` |
| Suspender: `scePowerRegisterCallback`; en `RESUME_COMPLETE` el disco reabre su imagen por ruta antes de la siguiente lectura | con `test_suspend_at=30` el hash sigue igual al de PC | mismo script |
| Carpeta: el directorio del juego sale de `argv[0]` | corre instalado en `ms0:/PSP/GAME/OTRA_CARPETA/` | `scripts/test-psp-carpeta.sh $E $J 25` |
| VMU: `psp/stubs/vmu.cpp` lee `VMU.BIN`; cada escritura va a `VMU.BIN.TMP`, `sceIoRename` sobre `VMU.BIN` y `sceIoSync` (parche `0005`) | la corrida 1 deja 131 072 B; la 2 los lee | `scripts/test-psp-vmu.sh $E $J 100` |
| Arranque | cuadros que avanzan, error claro sin `1ST_READ.BIN` | `scripts/test-psp-arranque.sh $E $J 30` |

`scripts/bateria.sh` corre todas en orden (`docs/pruebas-rapidas.md`).

## `ikaruga.cfg` de depuración

Junto al EBOOT; líneas `clave = valor`, `#` comenta. Las que usan estas pruebas: `rtc_seed`,
`hash_every`, `log_from`/`log_count` (el EBOOT escribe `writes.log` con el formato de `--write-log`
de PC para ubicar la primera escritura distinta), `test_exit_at`, `test_suspend_at`. Lista completa:
`docs/primer-uso.md`.

## Build `double`: igualdad total con el PC

Con `DREAM_FPU_DOUBLE=1` FIPR, FTRV y FMAC usan `double` (y FMA en FMAC) en el EBOOT y en el PC, y la
igualdad debe ser total. Dos diferencias de la FPU de la PSP frente a x86 lo impedían y las cubre el
parche `patches/dream-recomp/0010-soft-daz.patch`, activo solo con `DREAM_SOFT_DAZ=1` (que
`scripts/build-psp-ikaruga.sh` enciende con `DREAM_FPU_DOUBLE=1`; `DREAM_SOFT_DAZ=0` lo apaga para la
contraprueba):

- **DAZ en la entrada.** El guest corre con FPSCR.DN=1 (Katana arranca con `0x00040001`), que el
  runtime traduce a FTZ+DAZ en x86 y a `FCSR.FS` en la PSP. Con FS=1 la PSP vacía un *resultado*
  denormal pero usa una *entrada* denormal tal cual (`1e-39 * 1e10 = 0x0F4AD2FA`, donde x86 da 0).
  `ctx.h` envuelve `fr[]`/`xf[]` en `Freg`: al leerse para operar, un denormal vale ±0; las copias
  (`fmov`) guardan los bits crudos.
- **NaN del `double` por software.** El `double` de libgcc propaga otro signo de NaN que x86. En la
  rama `double` de `ops.h`, FIPR/FTRV/FMAC vacían un resultado `float` denormal y, si es NaN, lo
  recalculan con la regla de x86 (`x86fp::dot4`, `x86fp::fma`).

El build `float` compila `Freg = float`: sin cambio de comportamiento. La sonda `fpu:` del arranque
(`log_fpu_probe` en `psp/ikaruga/main.cpp`) imprime en el log cómo trata la FPU denormales y NaN.

`DREAM_FPU_DOUBLE=1 scripts/build-psp-ikaruga.sh $DREAM_RECOMP/build-rel/games/ikaruga/gen --variantes=mix`
construye el EBOOT `double`; el PC `double` se compila con
`-DCMAKE_CXX_FLAGS=-DDREAM_FPU_DOUBLE=1` (`docs/reproducir-pc.md`, §5.3).

```sh
scripts/test-psp-igual-pc-double.sh $E $J                  # write_hash igual en cada cuadro 1…2600
DREAM_FPU_DOUBLE=1 DREAM_SOFT_DAZ=0 scripts/build-psp-ikaruga.sh …/gen --variantes=mix
scripts/test-psp-igual-pc-double.sh --contraprueba $E $J   # sin 0010 diverge en el cuadro 1983
```

`test-psp-igual-pc-double.sh` corre `test-psp-igual-pc.sh` con las pulsaciones del capítulo 1,
`HASTA=2600` y `hash_every=1`, y compara todos los cuadros que registraron los dos lados. La PSP
tarda unos 1 500 s de PPSSPP en llegar al cuadro 2600.

## Qué no cubre

- Nada en hardware real: que el Allegrex respete `FCSR.RM` y `FCSR.FS` solo lo dice PPSSPP.
- HOME y suspensión son simulados desde el propio EBOOT (`sceKernelNotifyCallback`, eventos de
  energía y cierre del descriptor del disco); el menú HOME y una suspensión reales no se probaron.
- Igualdad con el PC: sin entrada hasta el cuadro 600; en `double`, con las pulsaciones del capítulo 1
  hasta el 2600. Con otra entrada o más allá puede aparecer otra diferencia de FPU.
- VMU: si se corta la energía entre `sceIoRemove` y `sceIoRename` solo queda `VMU.BIN.TMP` y el
  arranque no la recupera (plan, paso 70). Cada escritura de bloque reescribe 128 KB; su costo en una
  Memory Stick real no se midió.
