# ikaruga-psp

Port of **Ikaruga** (Treasure, Dreamcast 2002) to the **PlayStation Portable**, through static recompilation with [dcrecomp](https://github.com/sp00nznet/dcrecomp) (SH-4 → C).

**Status: phase 0, nothing playable yet.** We are measuring how far dcrecomp gets with the Dreamcast version on PC before writing the PSP layer.

## No game data here
This repository contains only our own code and docs. You need **your own copy** of the Dreamcast disc; the game executable and the C generated from it are produced locally and are never committed.

## Plan
1. Recompile the Dreamcast executable with dcrecomp and run it on PC.
2. PSP layer: PowerVR2 display lists → PSP GU, AICA sound → Media Engine, input, files from the Memory Stick.
3. Run on real hardware (PSP, or PS Vita with Adrenaline) and tune for 60 fps.

References: [sf64-psp](https://github.com/themriron2/sf64-psp) and [oot-psp](https://github.com/z2442/oot-psp), which solved the same problems for N64 decompilations.

## AI disclosure
This project is developed with AI assistance (Claude Code). A human reviews and decides every change.

## License
Our code is MIT (see `LICENSE`). Builds that include dcrecomp are distributed under GPL-2.0, dcrecomp's license. Ikaruga is © Treasure; this is an unofficial fan project.

---

# ikaruga-psp (español)

Port de **Ikaruga** (Treasure, Dreamcast 2002) a la **PSP** por recompilación estática con dcrecomp. **Estado: fase 0, todavía no se juega.** Necesitas **tu propia copia** del disco de Dreamcast; el ejecutable del juego y el C que se genera de él se producen en tu máquina y nunca se suben. Se desarrolla con ayuda de IA (Claude Code) y una persona revisa y decide cada cambio. Nuestro código es MIT; los builds que incluyen dcrecomp se distribuyen bajo GPL-2.0.
