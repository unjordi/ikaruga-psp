# Arranque del EBOOT en la PSP

Qué hace el EBOOT al arrancar y cómo se prueba (`scripts/test-psp-arranque.sh`). Las corridas de
medición del arranque están en `docs/mediciones/arranque-psp.md`.

## Qué corre

- `build/psp/ikaruga/EBOOT.PBP` es la variante `mix` (runtime, stubs y `main.cpp` a `-O2`; el C++
  generado a `-Os`) con `MEMSIZE=1`. `EBOOT-Os.PBP` (todo `-Os`) sale con `--variantes=mix,Os`.
  Tamaños y presupuesto: `docs/memoria-psp.md`.
- Lee de su propia carpeta (la toma de `argv[0]`; `ms0:/PSP/GAME/IKARUGA/` si no hay): `1ST_READ.BIN`
  y `DISCO.ISO` (el área HD del GD-ROM, volcada con `tools/volcar-area-hd.cpp`; receta en
  `docs/primer-uso.md`). El disco no va al repo. Con el ISO que arma `scripts/preparar-disco-ikaruga.sh`
  el guest diverge del PC desde el cuadro 4: no sirve para el oráculo de igualdad.
- Crea junto a sí `VMU.BIN` (tarjeta de memoria, 128 KiB) e `ikaruga.log`. `ikaruga.cfg` es opcional
  (`docs/primer-uso.md`, «Qué copiar a la memory stick»).
- `ikaruga.log` lleva una línea `tick` cada 60 cuadros del guest: `frames`, `cycles` (SH-4, 200 MHz),
  `guest_s`, `host_s`, `speed = guest_s / host_s`, `idle_skips`, `idle_cycles` y `pc` del guest. En
  PPSSPP `host_s` es el reloj emulado de la PSP.

## Idle-skip

Al arrancar el EBOOT llena la tabla de esperas con la sección `[idle]` de `ikaruga.toml`
(`0x0C0370BA = katana_wait_vblank`; `docs/idle-skip.md`). Un archivo `NO_IDLE_SKIP` en la carpeta del
EBOOT la deja vacía; `PROBAR_SIN_IDLE=1` en `scripts/probar-ikaruga-ppsspp.sh` lo crea. El log dice
`idle-skip: on|off`.

## Prueba

```sh
scripts/test-psp-arranque.sh build/psp/ikaruga/EBOOT.PBP $J 60
```

`$J` es la carpeta extraída del disco. Corre dos veces el EBOOT en PPSSPP:

- sin `1ST_READ.BIN` el log trae `error: cannot read ms0:/PSP/GAME/IKARUGA/1ST_READ.BIN; …` y el EBOOT
  sale con `sceKernelExitGame`;
- con el juego, los cuadros avanzan, el `pc` del guest toma varios valores y no aparece `fault` ni
  `stop`.

Falla si falta el error, si no hay cuadros, si el `pc` no cambia o si el guest se detuvo. Con el
mismo arranque, `scripts/test-psp-igual-pc.sh` compara contra el PC recompilado el hash de las
escrituras del guest (`docs/correctitud-psp.md`).

## Qué no cubre

Todo es PPSSPP; en una PSP real no se ha corrido.
