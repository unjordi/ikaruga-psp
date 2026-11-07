# ikaruga-psp

Port of **Ikaruga** (Treasure, Dreamcast 2002) to the **PlayStation Portable**, through static recompilation with [dream-recomp](https://github.com/phobos665/dream-recomp) (SH-4 → C++).

**Status: nothing playable on PSP yet.** On PC, dream-recomp already runs the Dreamcast version through chapter 1 with textures (see `docs/framework-comparacion.md`); next is the PSP layer.

## No game data here
This repository contains only our own code and docs. You need **your own copy** of the Dreamcast disc; the game executable and the C generated from it are produced locally and are never committed.

## Try it
From your own disc to the `ms0:/PSP/GAME/IKARUGA/` folder, tested end to end in PPSSPP (title screen only, not playable yet): [`docs/primer-uso.md`](docs/primer-uso.md) (Spanish).

## Plan
1. Recompile the Dreamcast executable with dream-recomp and run it on PC.
2. PSP layer: PowerVR2 display lists → PSP GU, AICA sound → Media Engine, input, files from the Memory Stick.
3. Run on real hardware (PSP, or PS Vita with Adrenaline) and tune for 60 fps.

References: [sf64-psp](https://github.com/themriron2/sf64-psp) and [oot-psp](https://github.com/z2442/oot-psp), which solved the same problems for N64 decompilations.

## AI disclosure
This project is developed with AI assistance (Claude Code). A human reviews and decides every change.

## License
Our code is MIT (see `LICENSE`). Builds that include dream-recomp are distributed under GPL-2.0, dream-recomp's license. Ikaruga is © Treasure; this is an unofficial fan project.

---

# ikaruga-psp (español)

Port de **Ikaruga** (Treasure, Dreamcast 2002) a la **PSP** por recompilación estática con dream-recomp. **Estado: todavía no corre en la PSP; en PC ya llega al capítulo 1.** Necesitas **tu propia copia** del disco de Dreamcast; el ejecutable del juego y el C que se genera de él se producen en tu máquina y nunca se suben. Se desarrolla con ayuda de IA (Claude Code) y una persona revisa y decide cada cambio. Nuestro código es MIT; los builds que incluyen dream-recomp se distribuyen bajo GPL-2.0.
