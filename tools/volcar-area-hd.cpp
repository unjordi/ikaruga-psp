// Dumps the high-density area of a GD-ROM image (any format dream-recomp's open_disc reads: CHD,
// GDI, CDI) as 2048-byte user sectors from LBA 45000 to the lead-out, the DISCO.ISO layout
// psp/stubs/disc.cpp serves. Prints the disc's track list so a layout with several HD tracks shows.
//   volcar-area-hd <imagen> <DISCO.ISO>
#include <cstdio>
#include <string>
#include <vector>

#include "dream/runtime/gdrom/disc.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <disc image> <DISCO.ISO>\n", argv[0]);
        return 2;
    }
    std::string err;
    auto disc = dream::gdrom::open_disc(argv[1], err);
    if (!disc) {
        std::fprintf(stderr, "%s: %s\n", argv[1], err.c_str());
        return 1;
    }
    for (const auto& t : disc->tracks())
        std::printf("track %u lba %u sectors %u size %u %s\n", t.number, t.lba, t.sectors,
            t.sector_size, t.data ? "data" : "audio");
    std::FILE* out = std::fopen(argv[2], "wb");
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    std::vector<std::uint8_t> sector(dream::gdrom::kUserBytes);
    const std::uint32_t end = disc->leadout_lba();
    std::uint32_t bad = 0;
    for (std::uint32_t lba = dream::gdrom::kHdAreaLba; lba < end; ++lba) {
        if (!disc->read_user(lba, sector.data())) {
            std::fill(sector.begin(), sector.end(), 0);
            ++bad;
        }
        if (std::fwrite(sector.data(), 1, sector.size(), out) != sector.size()) {
            std::fprintf(stderr, "write failed\n");
            return 1;
        }
    }
    std::fclose(out);
    std::printf("wrote %u sectors (%u unreadable, zero-filled) to %s\n",
        end - dream::gdrom::kHdAreaLba, bad, argv[2]);
    return bad == 0 ? 0 : 1;
}
