# Idle-skip de `kmWaitVBlank` (Ikaruga)

Cambio de código: `idleskip.patch` (un commit sobre dream-recomp `51ca276`, `git format-patch`),
aquí en `patches/dream-recomp-pc/0001-idle-skip-for-declared-wait-loops.patch`. Toca el traductor y el runtime: para usarlo en la PSP hay que regenerar el C++ con el traductor parchado y aplicar la parte del runtime en `scripts/build-psp-ikaruga.sh` (`patches/dream-recomp/0004-idle-skip-runtime.patch`; medido en docs/arranque-psp.md, «Idle-skip en la PSP»).

Revisión (2026-11-06 09:5x): reconstruido desde cero y medido con y sin idle-skip; guest idéntico (16000009449 ciclos, 4795 cuadros, pc 0x0c0196fc), host 9,84 s contra 22,32 s con la máquina cargada (CONFIRMADO).

## Bucle

`kmWaitVBlank` (guest `0x0c037080`, `games/ikaruga/symbols.tsv:544`) guarda en `r13` el contador
de VBlank (`0x8c10b688+0x940`), pone un timeout `0x100000` en la pila (`@r15` apunta a él) y gira
desde el bloque `0x0c0370ba`: `jsr @r14` a `kmyRequestCallback` con `r4` = 7 y luego 12,
decrementa el timeout, relee el contador y vuelve a `0x0c0370ba` mientras el contador no cambie y el
timeout no sea 0. Lo cambia el handler de la interrupción de VBlank.

## Diseño

- `games/<id>/<id>.toml` gana una tabla `[idle]`: bloque cabeza del bucle → tipo de bucle.
  Ikaruga declara `"0x0C0370BA" = "katana_wait_vblank"`. Documentado en `docs/game-config.md`
  («Idle loops»); `docs/runtime-interrupts.md` ya no lo lista como pendiente.
- Traductor: `dream-translate game` mete esas direcciones en `EmitOptions::idle_blocks`; al inicio
  de cada una se emite `idle_poll(c, m, pc)`. Sin `[idle]` el C++ generado es idéntico al de antes.
- Runtime: `idle_poll` → `Hooks::on_idle` (no-op por defecto) → `System::on_idle`. Compara la vuelta
  actual con la anterior en la misma cabeza. Salta solo si entre ambas no hubo interrupción
  entregada, `trapa` ni acceso a un dispositivo (contador nuevo `device_accesses` en el callback
  `on_device_access`), el `Ctx` entero coincide salvo `cycles`, `next_event` y `r3`, y el timeout
  (`@@r15`, igual a `r3`) bajó exactamente 1. Entonces con `d` = ciclos de una vuelta salta
  `k = min((next_event − cycles − 1) / d, timeout − 1)` vueltas: `cycles += k·d`, timeout y `r3`
  bajan `k`. La vuelta que alcanza `next_event` corre traducida, así que la interrupción cae en el
  mismo ciclo que sin salto, y el timeout nunca llega a 0 por un salto.
- Costo por sondeo en la PSP (`patches/dream-recomp/0012-idle-skip-cheap.patch`, mismas decisiones):
  las cabezas declaradas viven en una tabla lineal (`System::IdleTable`, hasta 8), la vuelta anterior
  (`idle_prev`) se compara en el sitio —`Ctx` salvo `r3`, `cycles` y `next_event`, en dos `memcmp`— y
  solo se copia entera cuando la vuelta difiere; una vuelta igual actualiza esos tres campos y el
  timeout. `k` se divide en 32 bits cuando `next_event − cycles − 1` y `d` caben. `IdleTurn` y el
  estado guardado no cambian.
- Desactivable: sin entrada `[idle]` no se emite nada; `--no-idle-skip` en el lanzador deja vacía
  la tabla del runtime (el `idle_poll` emitido queda como un retorno inmediato). El reporte añade
  `idle skips: N (T loop turns, C cycles)`.

## Números

Host: este equipo (Linux x86_64, build `build-rel`, `-DDREAM_DEV_INTERPRETER=OFF`, Release).
`1ST_READ.BIN` sha1 `f2524eb5…c6f9`; sin `[idle]` el `gen/` da el sha256 de referencia
`095e71ea…4333a`.

Comando (desde `juego/`, `X` vacío = con idle-skip, `X=--no-idle-skip` = sin él):

```sh
../build-rel/games/ikaruga/ikaruga_boot --config ../games/ikaruga/ikaruga.toml --vmu t.vmu \
  --max-seconds 80 --rtc-seed 1 --no-audio \
  --press start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200 \
  --report r.txt $X
```

| corrida | guest | host |
|---|---|---|
| binario sin el cambio | 80.000 s (16000009449 cycles), 4795 frames, pc 0x0c0196fc | 16.50 s (4.8×) |
| sin idle-skip 1 | 80.000 s (16000009449 cycles), 4795 frames, pc 0x0c0196fc | 17.95 s (4.5×) |
| sin idle-skip 2 | idem | 24.25 s (3.3×) |
| sin idle-skip 3 | idem | 17.33 s (4.6×) |
| con idle-skip 1 | 80.000 s (16000009449 cycles), 4795 frames, pc 0x0c0196fc | 9.00 s (8.9×) |
| con idle-skip 2 | idem | 10.26 s (7.8×) |
| con idle-skip 3 | idem | 9.31 s (8.6×) |

Con idle-skip, las tres corridas: `idle skips: 4951417 (194477510 loop turns, 10696263050 cycles)`,
o sea el 67 % de los ciclos de guest se adelantan en vez de ejecutarse. Todas con
`untranslated call targets: 0`. La corrida 2 sin idle-skip (24.25 s) es un valor atípico; con la
mediana, 17.95 s → 9.31 s (−48 %).

Ventana (Xvfb, `xvfb-run -a -s "-screen 0 1024x768x24"`, porque no hay `DISPLAY` en esta sesión),
`--window --max-seconds 50 --screenshot-at 2600`, mismos `--press`:

- con idle-skip: `guest: 50.000 s (10000010649 cycles), 2996 frames, pc 0x0c0370ba`, host 47.50 s;
- sin idle-skip: idéntico guest, host 52.27 s (con vsync el host queda atado al tiempo real).
- `screenshot-000.ppm` de ambas: `cmp` dice idénticas byte a byte. Muestra el capítulo 1: nave
  blanca sobre estructuras, HUD `1P`, `CHAIN STATUS`, vidas ×2, `PLEASE WAIT` arriba a la derecha.

`ctest` en `build-rel`: 6/6 (dcdisc.pytest omitido).
