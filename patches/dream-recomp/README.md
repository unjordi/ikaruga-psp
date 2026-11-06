# Patches to dream-recomp for the PSP build

`scripts/build-psp-ikaruga.sh` copies `third_party/dream-recomp/{runtime,render}` to
`build/psp/ikaruga/src` and applies these with `patch -p1`, in name order. The submodule is never
modified.

| Patch | Why the PSP needs it | Effect on the PC build |
|---|---|---|
| `0001-fenv-mips-fcsr.patch` | `runtime/src/fenv.cpp` stops with `#error "unsupported host ISA"` outside x86-64/ARM64. Adds a MIPS branch: flush-to-zero is FCSR bit 24 (FS), read/written with `cfc1`/`ctc1 $31`. The rounding mode is FCSR bits 1:0 (RM), written and read the same way: newlib-psp's `fesetround` is a stub that returns an error and leaves the FPU alone (`scripts/test-psp-redondeo.sh`). | none (new `#elif`) |
| `0002-explicit-min-clamp-types.patch` | On psp-gcc `std::int32_t`/`std::uint32_t` are `long`/`unsigned long`, so `std::min(u32, 255u)` or `std::clamp(s32, 0, 255)` cannot deduce one type. Spells the template argument out (`std::min<u32>`, `std::clamp<s32>`...) in `aica/mixer.cpp`, `render/src/{texture,display_list,framebuffer}.cpp`. | none (same type the PC deduces) |
| `0003-gdrom-optional-chd.patch` | `gdrom/disc.cpp` includes libchdr for `.chd` images. Wraps the CHD reader in `#ifndef DREAM_NO_CHD`; the PSP build defines it (decompressing zlib/LZMA hunks per sector costs CPU we do not have, docs/diseno-psp.md table 1 row 8). GDI and CDI stay. | none unless `DREAM_NO_CHD` is defined |
| `0005-maple-card-writer-hook.patch` | `MemoryCard::save()` writes with `std::ofstream` straight over the card, and `load()` goes through `std::filesystem`. Adds `MemoryCard::writer` (when set, `save()` hands it the image) and `set_image()`, so `psp/stubs/vmu.cpp` reads with `sceIo` and saves through a temporary file and `sceIoRename`. | none (`writer` unset) |

Flags instead of patches (`psp/ikaruga/Makefile`):

- `-std=gnu++20`, `-ffp-contract=off -frounding-math` as dream-recomp's `cmake/DreamTargetDefaults.cmake`.
- Exceptions and RTTI stay on: `sh4::rte()` unwinds guest frames by throwing `NonLocalReturn`, and
  `System` reports untranslated calls with `UntranslatedCall : std::runtime_error`.
- `-mno-explicit-relocs`, only for the generated units: with explicit `%hi`/`%lo` relocations psp-as
  is superlinear on the 45 MB `ikaruga.cpp` (the first 800k of 8.1M assembly lines take 7.9 s; the
  whole file had not finished after 26 min). With assembler macros the whole unit assembles in 3 s.
- `<filesystem>` (only `gdrom/disc.h`, `open_disc`) and `std::chrono` compile against psp-gcc 15.2's
  libstdc++ without changes; whether they work at run time on the PSP is not tested.
