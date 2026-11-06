# scripts

- `build-psp.sh [--limpio]`: builds `psp/hola` into `build/psp/hola/EBOOT.PBP` inside Docker with PSPDEV v20261001 pinned by sha256 (`psp/Dockerfile`); `--limpio` drops the toolchain image and rebuilds without cache. Needs: Docker.
- `probar-ppsspp.sh <EBOOT.PBP> <captura.png>`: boots the EBOOT in PPSSPP (flatpak `org.ppsspp.PPSSPP`) on Xvfb, saves a screenshot, exits non-zero if the boot fails. Needs: flatpak, xvfb-run, ImageMagick.
- `test-psp-hola.sh [--sin-limpio]`: acceptance test: clean build, OCR reads "ikaruga-psp" in the capture, a truncated EBOOT is rejected. Needs, besides the above: tesseract.

- `build-psp-ikaruga.sh <dir-gen> [--solo-o2] [--limpio]`: compiles dream-recomp's core (submodule + `patches/dream-recomp/*.patch` on a copy), the translated C++ in `<dir-gen>` (mounted read-only, never copied) and `psp/stubs` with psp-g++ at -O2 and -Os; leaves `build/psp/ikaruga/ikaruga.elf` (-O2) and `ikaruga-Os.elf`, and prints `psp-size` against ~52 MB. Needs: Docker, `git submodule update --init third_party/dream-recomp`.
- `psp/tamano-por-grupo.py <map>`: bytes of the linked image per origin (generated code, runtime, libstdc++, newlib...) from the ld map.
