# Idle-skip de `kmWaitVBlank` (Ikaruga)

Salta la espera de `kmWaitVBlank` hasta el siguiente evento del scheduler, en el PC recompilado y en
la PSP. El guest hace exactamente lo mismo (mismos ciclos, cuadros y `pc` finales); solo se ahorra
ejecutar la espera.

Piezas:

- Traductor y runtime del PC: `patches/dream-recomp-pc/0001-idle-skip-for-declared-wait-loops.patch`
  (sobre dream-recomp `51ca276`; `docs/reproducir-pc.md`, §5.2).
- Runtime de la PSP: `patches/dream-recomp/0004-idle-skip-runtime.patch` y
  `patches/dream-recomp/0012-idle-skip-cheap.patch`, que `scripts/build-psp-ikaruga.sh` aplica sobre una
  copia del submódulo. El `gen/` que se compila tiene que salir del traductor parchado: el C++ con
  `idle_poll` no enlaza contra un runtime sin `0004`, y un `gen/` sin `idle_poll` compila pero nunca salta.

## El bucle

`kmWaitVBlank` (guest `0x0c037080`, `games/ikaruga/symbols.tsv:544` de dream-recomp) guarda en `r13` el
contador de VBlank (`0x8c10b688+0x940`), pone un timeout `0x100000` en la pila (`@r15` apunta a él) y
gira desde el bloque `0x0c0370ba`: `jsr @r14` a `kmyRequestCallback` con `r4` = 7 y luego 12,
decrementa el timeout, relee el contador y vuelve a `0x0c0370ba` mientras el contador no cambie y el
timeout no sea 0. Lo cambia el handler de la interrupción de VBlank.

## Diseño

- `games/<id>/<id>.toml` tiene una tabla `[idle]`: bloque cabeza del bucle → tipo de bucle. Ikaruga
  declara `"0x0C0370BA" = "katana_wait_vblank"` (`docs/game-config.md`, «Idle loops», de dream-recomp
  parchado).
- Traductor: `dream-translate game` mete esas direcciones en `EmitOptions::idle_blocks`; al inicio de
  cada una se emite `idle_poll(c, m, pc)`. Sin `[idle]` el C++ generado es el de siempre.
- Runtime: `idle_poll` → `Hooks::on_idle` (no-op por omisión) → `System::on_idle`. Compara la vuelta
  actual con la anterior en la misma cabeza. Salta solo si entre ambas no hubo interrupción entregada,
  `trapa` ni acceso a un dispositivo (contador `device_accesses` en el callback `on_device_access`), el
  `Ctx` entero coincide salvo `cycles`, `next_event` y `r3`, y el timeout (`@@r15`, igual a `r3`) bajó
  exactamente 1. Entonces, con `d` = ciclos de una vuelta, salta `k = min((next_event − cycles − 1) / d,
  timeout − 1)` vueltas: `cycles += k·d`, timeout y `r3` bajan `k`. La vuelta que alcanza `next_event`
  corre traducida, así que la interrupción cae en el mismo ciclo que sin salto, y el timeout nunca llega
  a 0 por un salto.
- Costo por sondeo en la PSP (`0012`): las cabezas declaradas viven en una tabla lineal
  (`System::IdleTable`, hasta 8); la vuelta anterior (`idle_prev`) se compara en el sitio —`Ctx` salvo
  `r3`, `cycles` y `next_event`, en dos `memcmp`— y solo se copia entera cuando la vuelta difiere; una
  vuelta igual actualiza esos tres campos y el timeout. `k` se divide en 32 bits cuando `next_event −
  cycles − 1` y `d` caben. `IdleTurn` y el estado guardado (`docs/estado-guardado.md`) no cambian.
- Se apaga sin tocar el código: PC `--no-idle-skip` (el `idle_poll` emitido queda como un retorno
  inmediato); PSP, un archivo `NO_IDLE_SKIP` en la carpeta del EBOOT (`PROBAR_SIN_IDLE=1` en
  `scripts/probar-ikaruga-ppsspp.sh`). El reporte del PC añade `idle skips: N (T loop turns, C cycles)`;
  el log de la PSP lleva `idle_skips` e `idle_cycles` en cada línea `tick`.

## Qué se mide

En PC (build `build-rel`, `--no-audio`, `--rtc-seed 1`, 80 s de juego con las pulsaciones del capítulo
1), con y sin `--no-idle-skip` el guest es idéntico (16 000 009 449 ciclos, 4 795 cuadros, `pc 0x0c0196fc`)
y el host baja de ≈18 s a ≈9,3 s (mediana de tres corridas): se adelanta el 67 % de los ciclos del guest
y el reporte da `untranslated call targets: 0`. Con `--window` las capturas del cuadro 2600 son
idénticas byte a byte. En la PSP (PPSSPP) el arranque hasta el cuadro 600 es ≈5× más rápido
(`docs/mediciones/arranque-psp.md`).
