// Decodes one Tile Accelerator parameter stream (as written by ikaruga_boot --dump-ta) with
// dream::render::DisplayList and prints the same counts the PSP EBOOT logs for its captured render:
//   polygons N opaque N punch N translucent N vertices N
// The polygon count is the decoded stream's, without the background plane.
// Built by scripts/comparar-geometria.sh against the submodule's render/src/display_list.cpp.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "dream/render/display_list.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: contar-poligonos <ta-stream.bin>\n");
        return 2;
    }
    std::FILE* f = std::fopen(argv[1], "rb");
    if (!f) {
        std::fprintf(stderr, "%s: cannot open\n", argv[1]);
        return 1;
    }
    std::vector<std::uint32_t> words;
    std::uint32_t buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 4, 4096, f)) > 0) words.insert(words.end(), buf, buf + n);
    std::fclose(f);
    dream::render::DisplayList decoder;
    decoder.feed_stream(words.data(), words.size());
    const dream::render::Frame& frame = decoder.frame();
    using dream::render::ListType;
    std::printf("words %zu polygons %zu opaque %zu punch %zu translucent %zu vertices %zu\n",
                words.size(), frame.polygon_count(),
                frame.lists[static_cast<unsigned>(ListType::Opaque)].size(),
                frame.lists[static_cast<unsigned>(ListType::PunchThrough)].size(),
                frame.lists[static_cast<unsigned>(ListType::Translucent)].size(),
                frame.vertices.size());
    return 0;
}
