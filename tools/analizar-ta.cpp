// Counts which PVR features one Tile Accelerator stream (ikaruga_boot --dump-ta) exercises, to
// decide whether a video step of docs/plan.md is needed. Decoded with the submodule's
// dream::render::DisplayList, the same decoder the PSP EBOOT uses.
// Build: g++ -std=c++20 -O2 -Ithird_party/dream-recomp/render/include tools/analizar-ta.cpp \
//          third_party/dream-recomp/render/src/display_list.cpp -o build/host/analizar-ta
// Bit layout: Flycast core/hw/pvr/ta_structs.h (ISP_TSP, TSP, TCW, PCW).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "dream/render/display_list.h"

using u32 = std::uint32_t;
static std::map<std::string, unsigned> counts;
static void add(const std::string& k) { ++counts[k]; }

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: analizar-ta <ta-stream.bin>\n");
        return 2;
    }
    std::FILE* f = std::fopen(argv[1], "rb");
    if (!f) return 1;
    std::vector<u32> words;
    u32 buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 4, 4096, f)) > 0) words.insert(words.end(), buf, buf + n);
    std::fclose(f);
    dream::render::DisplayList d;
    d.feed_stream(words.data(), words.size());
    const auto& fr = d.frame();
    static const char* lname[5] = {"OP", "OPMV", "TR", "TRMV", "PT"};
    std::vector<float> zs;
    for (unsigned l = 0; l < 5; ++l) {
        for (const auto& p : fr.lists[l]) {
            const std::string L = lname[l];
            add(L + ".poly");
            const u32 isp = p.isp, tsp = p.tsp, tcw = p.tcw, pcw = p.pcw;
            add(L + ".cull" + std::to_string((isp >> 27) & 3));
            add(L + ".depth" + std::to_string((isp >> 29) & 7));
            if ((isp >> 26) & 1) add(L + ".zwrite_off");
            if ((pcw >> 2) & 1) add(L + ".pcw_offset");
            if ((pcw >> 6) & 1) add(L + ".pcw_volume");
            if ((pcw >> 7) & 1) add(L + ".pcw_shadow");
            if (p.tsp1 != 0xFFFFFFFFu) add(L + ".two_volume");
            add(L + ".userclip" + std::to_string(p.tile_clip));
            add(L + ".fog" + std::to_string((tsp >> 22) & 3));
            if ((tsp >> 21) & 1) add(L + ".color_clamp");
            const u32 src = (tsp >> 29) & 7, dst = (tsp >> 26) & 7;
            add(L + ".blend" + std::to_string(src) + "/" + std::to_string(dst));
            if ((tsp >> 25) & 1) add(L + ".src_select");
            if ((tsp >> 24) & 1) add(L + ".dst_select");
            if (l == 2 && ((tsp >> 22) & 3) != 2 && src != 4 && src != 1)
                add("TR.fog_with_src_not_alpha_or_one");
            bool offset_nonzero = false;
            for (u32 i = 0; i < p.count; ++i) {
                const auto& v = fr.vertices[p.first + i];
                if (v.offset & 0x00FFFFFFu) offset_nonzero = true;
                zs.push_back(v.z);
            }
            if (offset_nonzero) add(L + ".offset_color_nonzero");
            if (std::getenv("ANALIZAR_TA_POLIGONOS")) {  // one line per strip: list, bbox, state
                float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
                for (u32 i = 0; i < p.count; ++i) {
                    const auto& v = fr.vertices[p.first + i];
                    x0 = std::min(x0, v.x); x1 = std::max(x1, v.x);
                    y0 = std::min(y0, v.y); y1 = std::max(y1, v.y);
                }
                std::printf("P %s bbox %.0f,%.0f-%.0f,%.0f area %.0f blend %u/%u fog %u tex %u fmt %u off %d base0 %08x off0 %08x tcw %08x\n",
                    L.c_str(), x0, y0, x1, y1, (x1 - x0) * (y1 - y0), (tsp >> 29) & 7, (tsp >> 26) & 7,
                    (tsp >> 22) & 3, (pcw >> 3) & 1, (tcw >> 27) & 7, offset_nonzero ? 1 : 0,
                    fr.vertices[p.first].base, fr.vertices[p.first].offset, tcw);
            }
            {  // affine-vs-perspective proxy: spread of 1/w inside one strip
                float lo = 1e30f, hi = 0;
                for (u32 i = 0; i < p.count; ++i) {
                    const float z = fr.vertices[p.first + i].z;
                    lo = std::min(lo, z); hi = std::max(hi, z);
                }
                const float r = lo > 0 ? hi / lo : 0;
                if (r > 1.05f) add(L + ".w_spread>1.05");
                if (r > 1.5f) add(L + ".w_spread>1.5");
                if (r > 3.0f) add(L + ".w_spread>3");
            }
            if (((pcw >> 3) & 1) || ((isp >> 25) & 1)) {  // textured (PCW bit 3; ISP bit 25)
                add(L + ".tex");
                add(L + ".tex_fmt" + std::to_string((tcw >> 27) & 7));
                if ((tcw >> 30) & 1) add(L + ".tex_vq");
                if ((tcw >> 31) & 1) add(L + ".tex_mip");
                const u32 fmt = (tcw >> 27) & 7;
                if (fmt != 5 && fmt != 6) {
                    if ((tcw >> 26) & 1) add(L + ".tex_nontwiddled");
                    if (((tcw >> 26) & 1) && ((tcw >> 25) & 1)) add(L + ".tex_stride");
                }
                if ((tsp >> 17) & 3) add(L + ".tex_flipuv" + std::to_string((tsp >> 17) & 3));
                if ((tsp >> 15) & 3) add(L + ".tex_clampuv" + std::to_string((tsp >> 15) & 3));
                add(L + ".tex_filter" + std::to_string((tsp >> 13) & 3));
                add(L + ".tex_shading" + std::to_string((tsp >> 6) & 3));
                if ((tsp >> 19) & 1) add(L + ".ignore_tex_alpha");
                if (((tsp >> 19) & 1) && ((tsp >> 6) & 3) == 1) add(L + ".modulate+ignore_tex_alpha");
                if (((tsp >> 6) & 3) == 2) add(L + ".decal_alpha");
            }
        }
    }
    for (const auto& m : fr.modifiers) add("MV.tri.mode" + std::to_string((m.isp >> 29) & 7));
    for (const auto& [k, v] : counts) std::printf("%s %u\n", k.c_str(), v);
    std::sort(zs.begin(), zs.end());
    zs.erase(std::unique(zs.begin(), zs.end()), zs.end());
    if (!zs.empty()) {
        const double lo = zs.front(), hi = zs.back(), step = (hi - lo) / 65535.0;
        unsigned close = 0;
        for (std::size_t i = 1; i < zs.size(); ++i)
            if (zs[i] - zs[i - 1] < step) ++close;
        // per decade of 1/w: distinct values and distinct linear 16-bit buckets (PSP video.cpp)
        for (double d = 1e-5; d < 10; d *= 10) {
            std::size_t vals = 0; std::vector<long> b;
            for (float z : zs) if (z >= d && z < d * 10) { ++vals; b.push_back(std::lround((z - lo) / step)); }
            std::sort(b.begin(), b.end()); b.erase(std::unique(b.begin(), b.end()), b.end());
            if (vals) std::printf("z.decade %g values %zu buckets16 %zu\n", d, vals, b.size());
        }
        std::printf("z.min %g z.max %g ratio %g distinct %zu step16 %g pairs_closer_than_step %u\n",
                    lo, hi, lo > 0 ? hi / lo : 0.0, zs.size(), step, close);
    }
    std::printf("frame.min_z %g max_z %g sprites %u strips %u modifiers %zu unknown %u dropped %u\n",
                fr.min_z, fr.max_z, fr.sprites, fr.strips, fr.modifiers.size(),
                fr.unknown_parameters, fr.dropped_vertices);
    return 0;
}
