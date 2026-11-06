# Tamaño del `.elf` de Ikaruga para la PSP (fila 4)

Build: `scripts/build-psp-ikaruga.sh <dir-gen> --limpio` (desde cero: borra `build/psp/ikaruga`),
toolchain `ikaruga-psp-toolchain:v20261001` (psp-g++ 15.2.0), dream-recomp `51ca276` + 
`patches/dream-recomp/*.patch`, C++ generado de `~/code/ajenos/dr-rev/build-rel/games/ikaruga/gen`
(sha256 de la concatenación ordenada de sus archivos: `095e71eaeafdd6444586606cff3f40992541c705ab7eeb7e76eb7ffa9c94333a`,
CONFIRMADO con `ls $G | sort | while read f; do cat "$G/$f"; done | sha256sum`).
Los stubs de host (`psp/stubs/`) no hacen nada: el `.elf` enlaza pero no arranca el juego.

MB = MiB (1 048 576 B), igual que el presupuesto de `docs/diseno-psp.md` §2.

## 1. `psp-size`

Comando (por variante): `docker run --rm -v $PWD/build/psp/ikaruga/<O2|Os>:/w:ro --workdir /w ikaruga-psp-toolchain:v20261001 psp-size ikaruga.elf`

| Variante | text | data | bss | dec | dec en MB |
|---|---:|---:|---:|---:|---:|
| `-O2` (`build/psp/ikaruga/ikaruga.elf`) | 25 255 581 | 2 548 | 589 412 | 25 847 541 | **24,65** |
| `-Os` (`build/psp/ikaruga/ikaruga-Os.elf`) | 13 723 689 | 2 548 | 589 412 | 14 315 649 | **13,65** |

`text` de `psp-size` incluye `.rodata`, `.eh_frame` y `.gcc_except_table`. Por sección
(`psp-size -A ikaruga.elf`):

| Sección | -O2 | -Os |
|---|---:|---:|
| `.text` | 23 043 868 | 12 795 948 |
| `.rodata` | 1 147 160 | 345 192 |
| `.eh_frame` | 605 756 | 554 512 |
| `.gcc_except_table` | 457 421 | 26 661 |
| `.data` | 2 000 | 2 000 |
| `.bss` | 588 748 | 588 748 |

La variante `-Os` aplica `-Os` a todo (runtime, generado, stubs); las librerías del SDK
(libstdc++, newlib) vienen precompiladas y no cambian.

## 2. Desglose por grupo

Comando: `python3 scripts/psp/tamano-por-grupo.py build/psp/ikaruga/<O2|Os>/ikaruga.map`
(suma las secciones de entrada cargadas del mapa de `ld`, agrupadas por archivo de origen).
Salida completa en `build/psp/evidencia/tamano-por-grupo.txt`.

| Grupo | -O2 imagen (B) | -O2 bss | -O2 MB | -Os imagen (B) | -Os MB |
|---|---:|---:|---:|---:|---:|
| Código generado (`gen/*.cpp`) | 23 474 085 | 0 | **22,39** | 12 028 520 | **11,47** |
| libstdc++ (+ libsupc++) | 1 053 526 | 4 598 | 1,01 | 1 052 270 | 1,01 |
| Runtime dream-recomp (núcleo) | 486 785 | 557 181 | 1,00 | 401 579 | 0,91 |
| newlib (libc/libm) | 192 355 | 27 285 | 0,21 | 192 355 | 0,21 |
| libgcc + crt | 54 064 | 265 | 0,05 | 54 172 | 0,05 |
| `*.ltrans0.ltrans.o` (objeto LTO de una lib del SDK, no identificada) | 10 996 | 44 | 0,01 | 10 916 | 0,01 |
| Stubs + `main.cpp` nuestros | 3 288 | 0 | 0,00 | 3 237 | 0,00 |
| pspsdk | 196 | 0 | 0,00 | 196 | 0,00 |
| **Total** | 25 275 295 | 589 373 | **24,67** | 13 743 245 | **13,67** |

El total del script difiere de `psp-size` en ≈20 KB (relleno de alineación y secciones del SDK
como `.sceStub.text`/`.lib.*` que el script no cuenta).

Del código generado, por sección (misma salida):

| | .text | .rodata | .eh_frame | .gcc_except_table |
|---|---:|---:|---:|---:|
| -O2 | 21 651 192 | 928 289 | 464 396 | 430 188 |
| -Os | 11 482 856 | 135 076 | 410 568 | 0 |

Referencia x86-64 (`docs/diseno-psp.md` §2): `text` del release = 13 146 938 B, compilado a `-O1`.
En MIPS el generado a `-O2` sale 1,65× eso (21,65 MB de `.text`); a `-Os`, 0,87×.
La estimación de §2 (13–20 MB) queda superada por -O2 y por debajo con -Os.

## 3. Contra el presupuesto (≈52 MB útiles, PSP-2000/3000 con `MEMSIZE=1`)

Cuenta: imagen del elf (`dec` de `psp-size`) + guest 28,1 MB (`docs/diseno-psp.md` §2, SEGÚN
`memory-footprint-study.md`). La suma la imprime el propio script al final del build
(`build/psp/evidencia/build-limpio.log`).

| | -O2 | -Os |
|---|---:|---:|
| Imagen del elf | 24,65 MB | 13,65 MB |
| + Guest (RAM 16 + VRAM 8 + ARAM 2 + Boot ROM 2 + flash/OCRAM 0,13) | 28,1 MB | 28,1 MB |
| **Subtotal** | **52,75 MB** | **41,75 MB** |
| Resto de §2 [INFER]: caché de texturas 1–2 MB, lista GE ×2 + audio + pilas + heap newlib 1–2 MB | 2–4 MB | 2–4 MB |
| **Total estimado** | **≈55–57 MB: NO entra** | **≈44–46 MB: entra, ≈6–8 MB de margen** |

- **-O2 no entra**: sólo imagen + guest ya pasa de 52 MB, antes de texturas, pilas y heap.
  Quitar la Boot ROM (recorte 1 de §2, −2 MB) lo deja en ≈53–55 MB: sigue sin entrar.
- **-Os entra** con margen; es la variante a usar mientras no haya medida de velocidad.
- Recortes adicionales vistos aquí, no aplicados:
  - `.eh_frame` + `.gcc_except_table` del generado: 0,9 MB a -O2, 0,4 MB a -Os. Compilar el
    generado con `-fno-exceptions`/sin tablas de unwind no es seguro: `sh4::rte()` lanza
    `NonLocalReturn` a través de frames del código generado.
  - Mezcla: `-Os` para el código frío y `-O2` para el caliente (requiere la cobertura de la fila 4b).
  - libstdc++ pesa 1 MB; no miré qué objetos lo arrastran (`grep libstdc++ ikaruga.map`).

## 4. Cómo se compiló (resumen; detalles en `patches/dream-recomp/README.md`)

- Runtime compilado: `runtime/src/` (fenv, sh4/*, mem, sched, holly/*, pvr/*, system, gdrom/disc,
  hle/*, maple, aica/*) + `render/src/{display_list,texture,framebuffer,background,fog}.cpp`.
  Fuera: `render/src/vk/*`, `render/src/input*.cpp`, `overlay.cpp`, `audio/` (SDL3),
  `runtime/boot/boot_main.cpp` (archivos, reloj, volcados), `devinterp/` (sólo build de desarrollo).
- Parches: `0001` fenv para MIPS (FCSR.FS), `0002` tipos explícitos en `std::min/max/clamp`
  (`int32_t` es `long` en psp-gcc), `0003` CHD opcional (`DREAM_NO_CHD`).
- Flags: `-G0 -ffp-contract=off -frounding-math -std=gnu++20`; excepciones y RTTI activas;
  `-mno-explicit-relocs` sólo para el generado (psp-as seguía sin terminar a los 26 min con relocs
  explícitos sobre las 8,1 M líneas de ensamblador de `ikaruga.cpp`; sin ellos, 3 s;
  `build/psp/evidencia/sonda-gas-no-explicit-relocs.log`).
- Tiempos del build desde cero (32 hilos): -O2 terminó ≈5 min tras empezar, -Os ≈2 min después
  (marcas de `stat` de los `.elf`: 23:53:13 y 23:55:24). `cc1plus` sólo sobre `ikaruga.cpp`
  a -O2 con `-mno-explicit-relocs`: 311 s (`build/psp/evidencia/sonda-gas-no-explicit-relocs.log`).

## Qué NO verifiqué y dónde está mi punto ciego

- **El elf no se ejecutó** (ni en PPSSPP ni en hardware): los stubs no leen el `1ST_READ.BIN` ni
  dibujan. Que enlace no dice que el runtime funcione en MIPS: `<filesystem>`, `std::chrono`,
  `fesetround(FE_TOWARDZERO)` de newlib sobre la FPU del Allegrex y el unwind de `NonLocalReturn`
  compilan, pero ninguno se probó en ejecución.
- **El parche de fenv** usa FCSR bit 24 (FS) como flush-to-zero; no comprobé que el Allegrex lo
  implemente igual que MIPS32 ni que `fesetround` de newlib-psp toque el FCSR real.
- **`-mno-explicit-relocs`** puede generar código algo distinto (macros del ensamblador en vez de
  pares `%hi/%lo` programados por gcc); no medí su efecto en tamaño ni velocidad contra la versión
  con relocs explícitos, porque esa nunca terminó de ensamblar.
- **El presupuesto de 52 MB** y el resto (texturas, GE, pilas) vienen de §2 de `diseno-psp.md`, no
  medidos aquí. Tampoco conté memoria dinámica del runtime fuera de los 28,1 MB del guest (vectores,
  `unordered_map` del ABI, colas de AICA/PVR): la `bss` del runtime (0,56 MB) sí está, el heap no.
- **No medí velocidad**: -Os podría ser bastante más lento que -O2 en el bucle caliente.
- El grupo `ltrans` (11 KB) no lo identifiqué; lo atribuyo a una lib del SDK con LTO.
- **Evidencia**: el sandbox no me dejó escribir en `~/code/ajenos/ikaruga-tareas/fila4-evidencia/`
  (`SANDBOX_ESCRIBE_FUERA`); los logs quedaron en `build/psp/evidencia/` del worktree y hay que
  copiarlos a mano antes de borrarlo.
