# scripts

- `build-psp.sh [--limpio]`: builds `psp/hola` into `build/psp/hola/EBOOT.PBP` inside Docker with PSPDEV v20261001 pinned by sha256 (`psp/Dockerfile`); `--limpio` drops the toolchain image and rebuilds without cache. Needs: Docker.
- `probar-ppsspp.sh <EBOOT.PBP> <captura.png>`: boots the EBOOT in PPSSPP (flatpak `org.ppsspp.PPSSPP`) on Xvfb, saves a screenshot, exits non-zero if the boot fails. Needs: flatpak, xvfb-run, ImageMagick.
- `test-psp-hola.sh [--sin-limpio]`: acceptance test: clean build, OCR reads "ikaruga-psp" in the capture, a truncated EBOOT is rejected. Needs, besides the above: tesseract.
