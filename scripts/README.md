# scripts

- `build-psp.sh [--limpio]`: builds `psp/hola` into `build/psp/hola/EBOOT.PBP` inside Docker with PSPDEV v20261001 pinned by sha256 (`psp/Dockerfile`); `--limpio` drops the toolchain image and rebuilds without cache. Needs: Docker.
- `probar-ppsspp.sh <EBOOT.PBP> <captura.png>`: boots the EBOOT in PPSSPP (flatpak `org.ppsspp.PPSSPP`) on Xvfb, saves a screenshot, exits non-zero if the boot fails. Needs: flatpak, xvfb-run, ImageMagick.
- `test-psp-hola.sh [--sin-limpio]`: acceptance test: clean build, OCR reads "ikaruga-psp" in the capture, a truncated EBOOT is rejected. Needs, besides the above: tesseract.

- `build-psp-ikaruga.sh <dir-gen> [--solo-o2] [--limpio]`: compiles dream-recomp's core (submodule + `patches/dream-recomp/*.patch` on a copy), the translated C++ in `<dir-gen>` (mounted read-only, never copied) and `psp/stubs` with psp-g++ at -O2 and -Os; leaves `build/psp/ikaruga/ikaruga.elf` (-O2) and `ikaruga-Os.elf`, and prints `psp-size` against ~52 MB; the -Os variant is also packed as `build/psp/ikaruga/EBOOT.PBP` (MEMSIZE=1). Needs: Docker, `git submodule update --init third_party/dream-recomp`.
- `psp/tamano-por-grupo.py <map>`: bytes of the linked image per origin (generated code, runtime, libstdc++, newlib...) from the ld map.
- `preparar-disco-ikaruga.sh <dir-extraido> <DISCO.ISO>`: ISO 9660 image of the extracted disc's game area (sectors numbered from LBA 45000, IP.BIN in the system area) that the PSP build reads as the GD-ROM. Needs: genisoimage or mkisofs.
- `probar-ikaruga-ppsspp.sh <EBOOT.PBP> <dir-juego-extraido|-> <segundos> [salida.log]`: runs the headless EBOOT in PPSSPP on a temporary memory stick with the game data, for the given seconds, and copies `ms0:/PSP/GAME/IKARUGA/ikaruga.log`. Needs: flatpak `org.ppsspp.PPSSPP`, xvfb-run, genisoimage.
- `test-psp-arranque.sh <EBOOT.PBP> <dir-juego-extraido> [segundos]`: acceptance test: without 1ST_READ.BIN the log has a clear error; with the game the guest frames advance and its PC changes.
