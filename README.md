# ikaruga-psp

Port of **Ikaruga** (Treasure, Dreamcast 2002) to the **PlayStation Portable**, through static recompilation with [dream-recomp](https://github.com/phobos665/dream-recomp) (SH-4 → C++).

**Status: runs in PPSSPP, not playable and not tried on a real PSP.** The EBOOT boots from your own disc and reaches the title screen, the mode menu and chapter 1, writing the same memory as the recompiled PC build at boot. Against Flycast (same disc) the image is close at the title, the mode menu and the side columns of chapter 1; the centre of chapter 1 still differs. Speed in PPSSPP is ≈0.086x of real time (≈5 fps) in chapter 1; playable needs ≈30 fps. There is no audio yet, and chapters 2 to 5 and the bosses have not been walked through. What is left is in [`docs/plan.md`](docs/plan.md): matching Flycast scene by scene, speed, audio, the rest of the game, and real hardware.

## No game data here
This repository contains only our own code and docs. You need **your own copy** of the Dreamcast disc; the game executable and the C generated from it are produced locally and are never committed.

## Try it
From your own disc to the `ms0:/PSP/GAME/IKARUGA/` folder, tested end to end in PPSSPP: [`docs/primer-uso.md`](docs/primer-uso.md) (Spanish). Index of all the docs: [`docs/README.md`](docs/README.md); scripts: [`scripts/README.md`](scripts/README.md).

## How it works
1. dream-recomp translates the Dreamcast executable to C++ on PC (`docs/reproducir-pc.md`).
2. The PSP build compiles that C++ with dream-recomp's runtime (patched, `patches/dream-recomp/`) and our host layer (`psp/stubs/`): PowerVR2 display lists → PSP GE, controls from the PSP pad, files from the Memory Stick, the memory card as a file.
3. Tests run the EBOOT in PPSSPP: images against Flycast, memory writes against the recompiled PC build, speed with PPSSPP's own limiter.

References: [sf64-psp](https://github.com/themriron2/sf64-psp) and [oot-psp](https://github.com/z2442/oot-psp), which solved the same problems for N64 decompilations.

## AI disclosure
This project is developed with AI assistance (Claude Code). A human reviews and decides every change.

## License
Our code is MIT (see `LICENSE`). Builds that include dream-recomp are distributed under GPL-2.0, dream-recomp's license. Ikaruga is © Treasure; this is an unofficial fan project.

---

# ikaruga-psp (español)

Port de **Ikaruga** (Treasure, Dreamcast 2002) a la **PSP** por recompilación estática con dream-recomp. **Estado:** corre en PPSSPP; no es jugable ni se ha probado en una PSP real. Desde tu disco llega al título, al menú de modos y al capítulo 1, y al arrancar escribe la misma memoria que el PC recompilado; contra Flycast la imagen se parece en el título, el menú y las columnas del capítulo 1, y el centro del capítulo 1 todavía difiere. En PPSSPP va a ≈0,086x del tiempo real (≈5 fps) en el capítulo 1; jugable pide ≈30. Sin audio todavía. Lo que falta está en [`docs/plan.md`](docs/plan.md). Necesitas **tu propia copia** del disco de Dreamcast; el ejecutable del juego y el C que se genera de él se producen en tu máquina y nunca se suben. Se desarrolla con ayuda de IA (Claude Code) y una persona revisa y decide cada cambio. Nuestro código es MIT; los builds que incluyen dream-recomp se distribuyen bajo GPL-2.0.
