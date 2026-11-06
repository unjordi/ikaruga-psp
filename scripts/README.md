# scripts

- `build-psp.sh [--limpio]`: builds `psp/hola` into `build/psp/hola/EBOOT.PBP` inside Docker with PSPDEV v20261001 pinned by sha256 (`psp/Dockerfile`); `--limpio` drops the toolchain image and rebuilds without cache. Needs: Docker.
- `probar-ppsspp.sh <EBOOT.PBP> <captura.png>`: boots the EBOOT in PPSSPP (flatpak `org.ppsspp.PPSSPP`) on Xvfb, saves a screenshot, exits non-zero if the boot fails. Needs: flatpak, xvfb-run, ImageMagick.
- `test-psp-hola.sh [--sin-limpio]`: acceptance test: clean build, OCR reads "ikaruga-psp" in the capture, a truncated EBOOT is rejected. Needs, besides the above: tesseract.

- `build-psp-ikaruga.sh <dir-gen> [--variantes=mix,Os] [--solo-o2] [--limpio]`: compiles dream-recomp's core (submodule + `patches/dream-recomp/*.patch` on a copy), the translated C++ in `<dir-gen>` (mounted read-only, never copied) and `psp/stubs` with psp-g++, one directory per variant: `mix` (runtime and stubs `OPT_RT=-O2`, generated `OPT_GEN=-Os`; default, `build/psp/ikaruga/ikaruga.elf` and `EBOOT.PBP`), `Os` (all -Os; `ikaruga-Os.elf`, `EBOOT-Os.PBP`), `O2` (all -O2; `ikaruga-O2.elf`, no EBOOT). `OPT_RT`/`OPT_GEN` in the environment override the `mix` flags. EBOOTs use MEMSIZE=1; prints `psp-size` against ~52 MB. Needs: Docker, `git submodule update --init third_party/dream-recomp`.
- `psp/tamano-por-grupo.py <map>`: bytes of the linked image per origin (generated code, runtime, libstdc++, newlib...) from the ld map.
- `preparar-disco-ikaruga.sh <dir-extraido> <DISCO.ISO>`: ISO 9660 image of the extracted disc's game area (sectors numbered from LBA 45000, IP.BIN in the system area) that the PSP build reads as the GD-ROM. Needs: genisoimage or mkisofs.
- `probar-ikaruga-ppsspp.sh <EBOOT.PBP> <dir-juego-extraido|-> <segundos> [salida.log]`: runs the headless EBOOT in PPSSPP on a temporary memory stick with the game data, for the given seconds, and copies `ms0:/PSP/GAME/IKARUGA/ikaruga.log`. Needs: flatpak `org.ppsspp.PPSSPP`, xvfb-run, genisoimage.
- `test-psp-arranque.sh <EBOOT.PBP> <dir-juego-extraido> [segundos]`: acceptance test: without 1ST_READ.BIN the log has a clear error; with the game the guest frames advance and its PC changes.
- `test-psp-redondeo.sh [dir-parches]`: builds psp/pruebas/redondeo against the patched runtime and checks in PPSSPP that toward-zero truncates 1/3 and nearest rounds it up (FCSR.RM).
- `test-psp-rtc.sh <EBOOT.PBP> <dir-juego-extraido> [segundos]`: `rtc_seed = 1000` in ikaruga.cfg gives the expected AICA RTC at frame 60.
- `test-psp-igual-pc.sh <EBOOT.PBP> <dir-juego-extraido> [segundos]`: PC <-> PSP oracle: compares `write_hash` at frames 60..600 (no input, RTC seed 1) and fails at the first different frame. Needs `DREAM_RECOMP` with the PC launcher built and `PROBAR_DISCO` with the real disc's HD area (`tools/volcar-area-hd.cpp`).
- `test-psp-salida-suspension.sh <EBOOT.PBP> <dir-juego-extraido>`: HOME exit callback and a simulated suspend/resume (debug keys of ikaruga.cfg); after the resume the hash must still equal PC's.
- `test-psp-carpeta.sh <EBOOT.PBP> <dir-juego-extraido> [segundos]`: the EBOOT runs from `ms0:/PSP/GAME/OTRA_CARPETA/`.
- `test-psp-vmu.sh <EBOOT.PBP> <dir-juego-extraido> [segundos]`: two runs; the first saves VMU.BIN, the second reads it.
