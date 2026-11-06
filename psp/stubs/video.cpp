// Rows 1-4 of docs/diseno-psp.md: draws the Frame that dream::render decodes from the Tile
// Accelerator stream with the GE, untextured, at 480x272, in through mode (GU_TRANSFORM_2D) with
// the 640x480 -> 480x272 scale done while converting the vertices.
//
// eDRAM layout (2 MB): two 512x272 RGBA8888 colour buffers (544 KB each) and a 16-bit depth
// buffer (272 KB). Each render is drawn into the back buffer, waited for, and swapped in, so the
// front buffer always holds the last complete render.
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "dream/render/display_list.h"
#include "dream/render/tsp.h"
#include "host.h"

namespace ikaruga::host {

namespace {

constexpr int kWidth = 480, kHeight = 272, kStride = 512;
constexpr std::uint32_t kColourBytes = kStride * kHeight * 4;
constexpr float kScaleX = 480.0f / 640.0f, kScaleY = 272.0f / 480.0f;

// One display list per render: a handful of commands per strip.
alignas(64) std::uint32_t g_list[256 * 1024];
constexpr int kStripListBytes = 256;

// GU_COLOR_8888 | GU_VERTEX_32BITF: colour first, then x, y, z.
struct GeVertex {
    std::uint32_t colour;  // 0xAABBGGRR
    float x, y, z;
};
constexpr int kVertexType = GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D;

std::vector<GeVertex> g_vertices;
bool g_ready = false;
void (*g_trace)(const char*, ...) = nullptr;
bool g_enabled = true;
bool g_depth = true;
std::uint32_t g_front = kColourBytes;  // eDRAM offset of the buffer on screen
std::uint32_t g_back = 0;

// ISP/TSP instruction word fields (the same ones the Vulkan backend reads).
constexpr std::uint32_t isp_depth_mode(std::uint32_t isp) { return (isp >> 29) & 7u; }
constexpr bool isp_z_write_disable(std::uint32_t isp) { return ((isp >> 26) & 1u) != 0; }

// PowerVR depth compare (on 1/w, larger is nearer) to the GE's, whose depth also grows nearer.
int ge_depth_func(std::uint32_t mode) {
    static const int kMap[8] = {GU_NEVER, GU_LESS,     GU_EQUAL,  GU_LEQUAL,
                                GU_GREATER, GU_NOTEQUAL, GU_GEQUAL, GU_ALWAYS};
    return kMap[mode & 7u];
}

// PowerVR blend instruction (0 zero, 1 one, 2 other colour, 3 inverse other colour, 4 source
// alpha, 5 inverse source alpha, 6 destination alpha, 7 inverse destination alpha) to the GE's
// factor; zero and one become GU_FIX with a fixed colour.
void ge_blend_factor(std::uint32_t instr, int& factor, unsigned& fix) {
    switch (instr & 7u) {
        case 0: factor = GU_FIX; fix = 0x000000; break;
        case 1: factor = GU_FIX; fix = 0xFFFFFF; break;
        case 2: factor = 0; fix = 0; break;  // GU_DST_COLOR as source, GU_SRC_COLOR as destination
        case 3: factor = 1; fix = 0; break;
        case 4: factor = GU_SRC_ALPHA; fix = 0; break;
        case 5: factor = GU_ONE_MINUS_SRC_ALPHA; fix = 0; break;
        case 6: factor = GU_DST_ALPHA; fix = 0; break;
        default: factor = GU_ONE_MINUS_DST_ALPHA; fix = 0; break;
    }
}

std::uint32_t argb_to_abgr(std::uint32_t c, bool use_alpha) {
    const std::uint32_t a = use_alpha ? (c >> 24) : 0xFFu;
    return (a << 24) | ((c & 0xFFu) << 16) | (c & 0xFF00u) | ((c >> 16) & 0xFFu);
}

}  // namespace

bool video_init() {
    sceGuInit();
    sceGuStart(GU_DIRECT, g_list);
    sceGuDrawBuffer(GU_PSM_8888, reinterpret_cast<void*>(g_back), kStride);
    sceGuDispBuffer(kWidth, kHeight, reinterpret_cast<void*>(g_front), kStride);
    sceGuDepthBuffer(reinterpret_cast<void*>(2 * kColourBytes), kStride);
    sceGuOffset(2048 - kWidth / 2, 2048 - kHeight / 2);
    sceGuViewport(2048, 2048, kWidth, kHeight);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, kWidth, kHeight);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuShadeModel(GU_SMOOTH);
    sceGuClearColor(0xFF000000u);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
    g_ready = true;
    return true;
}

void video_set_drawing(bool enabled) { g_enabled = enabled; }
void video_set_depth_test(bool enabled) { g_depth = enabled; }
void video_set_trace(void (*trace)(const char*, ...)) { g_trace = trace; }

VideoStats video_draw_frame(const dream::render::Frame& frame, float alpha_ref) {
    VideoStats stats;
    if (!g_ready)
        return stats;

    // Depth: the frame's 1/w range spread over the 16-bit buffer, larger nearer.
    const float zmin = frame.min_z, zmax = frame.max_z;
    // 0 is the cleared value, so drawn depth starts at 1 and a GREATER test against an empty
    // pixel passes.
    const float zscale = zmax > zmin ? 65534.0f / (zmax - zmin) : 0.0f;
    const float zbias = zmax > zmin ? 1.0f : 32768.0f;

    struct Item {
        const dream::render::Polygon* poly;
        float depth;
        bool blended, alpha_test;
    };
    std::vector<Item> items;
    items.reserve(frame.polygon_count());
    const auto add_list = [&](dream::render::ListType list, bool blended, bool alpha_test) {
        for (const auto& p : frame.lists[static_cast<unsigned>(list)]) {
            float nearest = 0.0f;
            for (std::uint32_t i = 0; i < p.count; ++i)
                nearest = std::max(nearest, frame.vertices[p.first + i].z);
            items.push_back({&p, nearest, blended, alpha_test});
        }
    };
    add_list(dream::render::ListType::Opaque, false, false);
    add_list(dream::render::ListType::PunchThrough, false, true);
    const std::size_t translucent_start = items.size();
    add_list(dream::render::ListType::Translucent, true, false);
    // Translucent strips back to front: the smallest 1/w is the farthest.
    std::stable_sort(items.begin() + static_cast<std::ptrdiff_t>(translucent_start), items.end(),
                     [](const Item& a, const Item& b) { return a.depth < b.depth; });

    // Vertices converted once, per polygon, since the alpha a vertex carries depends on its
    // polygon's TSP word.
    g_vertices.resize(frame.vertices.size());
    for (const Item& item : items) {
        const auto& p = *item.poly;
        const bool use_alpha = dream::render::tsp_use_alpha(p.tsp) != 0;
        for (std::uint32_t i = 0; i < p.count; ++i) {
            const dream::render::Vertex& v = frame.vertices[p.first + i];
            GeVertex& o = g_vertices[p.first + i];
            o.colour = argb_to_abgr(v.base, use_alpha);
            o.x = v.x * kScaleX;
            o.y = v.y * kScaleY;
            o.z = std::clamp(zbias + (v.z - zmin) * zscale, 1.0f, 65535.0f);
        }
    }
    sceKernelDcacheWritebackRange(g_vertices.data(),
                                  static_cast<unsigned>(g_vertices.size() * sizeof(GeVertex)));

    sceGuStart(GU_DIRECT, g_list);
    sceGuDrawBufferList(GU_PSM_8888, reinterpret_cast<void*>(g_back), kStride);
    sceGuClearColor(0xFF000000u);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    if (g_depth)
        sceGuEnable(GU_DEPTH_TEST);
    else
        sceGuDisable(GU_DEPTH_TEST);
    const unsigned ref = static_cast<unsigned>(std::clamp(alpha_ref, 0.0f, 1.0f) * 255.0f);
    sceGuAlphaFunc(GU_GEQUAL, static_cast<int>(ref), 0xFF);
    if (g_enabled) {
        for (const Item& item : items) {
            // Room for one strip's state and draw plus the closing commands; the rest of the
            // render is dropped rather than written past the end of g_list.
            if (sceGuCheckList() + kStripListBytes > static_cast<int>(sizeof g_list)) {
                stats.dropped = static_cast<std::uint32_t>(&item - items.data());
                stats.dropped = static_cast<std::uint32_t>(items.size()) - stats.dropped;
                break;
            }
            const auto& p = *item.poly;
            sceGuDepthFunc(ge_depth_func(isp_depth_mode(p.isp)));
            // GE depth mask: true disables writes. Translucent surfaces test but do not write.
            sceGuDepthMask((item.blended || isp_z_write_disable(p.isp)) ? GU_TRUE : GU_FALSE);
            if (item.blended) {
                int src = 0, dst = 0;
                unsigned fix_src = 0, fix_dst = 0;
                ge_blend_factor(dream::render::tsp_src_instr(p.tsp), src, fix_src);
                ge_blend_factor(dream::render::tsp_dst_instr(p.tsp), dst, fix_dst);
                sceGuEnable(GU_BLEND);
                sceGuBlendFunc(GU_ADD, src, dst, fix_src, fix_dst);
                ++stats.blended;
            } else {
                sceGuDisable(GU_BLEND);
            }
            if (item.alpha_test)
                sceGuEnable(GU_ALPHA_TEST);
            else
                sceGuDisable(GU_ALPHA_TEST);
            sceGuDrawArray(GU_TRIANGLE_STRIP, kVertexType, static_cast<int>(p.count), nullptr,
                           g_vertices.data() + p.first);
            ++stats.drawn;
            if (g_trace) {
                const GeVertex& v0 = g_vertices[p.first];
                g_trace("strip list %d n %u isp %08x tsp %08x func %d mask %d colour %08x "
                        "x %.1f y %.1f z %.1f",
                        item.blended ? 2 : (item.alpha_test ? 4 : 0), p.count, p.isp, p.tsp,
                        ge_depth_func(isp_depth_mode(p.isp)),
                        (item.blended || isp_z_write_disable(p.isp)) ? 1 : 0, v0.colour, v0.x,
                        v0.y, v0.z);
            }
        }
    }
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDepthMask(GU_FALSE);
    stats.list_bytes = static_cast<std::uint32_t>(sceGuCheckList());
    stats.list_capacity = sizeof g_list;
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceGuSwapBuffers();
    std::swap(g_front, g_back);
    return stats;
}

// The GE copies the front buffer to main memory: the CPU reading eDRAM directly can miss what the
// GE drew (an emulator may keep it on the host GPU until a transfer asks for it).
bool video_capture(std::vector<std::uint8_t>& rgb, unsigned& width, unsigned& height) {
    if (!g_ready)
        return false;
    static std::vector<std::uint32_t> copy;
    copy.assign(static_cast<std::size_t>(kStride) * kHeight + 16, 0);
    auto* dst = reinterpret_cast<std::uint32_t*>(
        (reinterpret_cast<std::uintptr_t>(copy.data()) + 63) & ~std::uintptr_t{63});
    sceKernelDcacheWritebackInvalidateRange(dst, kColourBytes);
    void* src = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(sceGeEdramGetAddr()) + g_front);
    sceGuStart(GU_DIRECT, g_list);
    sceGuCopyImage(GU_PSM_8888, 0, 0, kWidth, kHeight, kStride, src, 0, 0, kStride, dst);
    sceGuTexSync();
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceKernelDcacheInvalidateRange(dst, kColourBytes);
    width = kWidth;
    height = kHeight;
    rgb.resize(static_cast<std::size_t>(kWidth) * kHeight * 3);
    std::uint8_t* out = rgb.data();
    for (int y = 0; y < kHeight; ++y) {
        const std::uint32_t* row = dst + static_cast<std::size_t>(y) * kStride;
        for (int x = 0; x < kWidth; ++x) {
            const std::uint32_t c = row[x];  // 0xAABBGGRR
            *out++ = static_cast<std::uint8_t>(c);
            *out++ = static_cast<std::uint8_t>(c >> 8);
            *out++ = static_cast<std::uint8_t>(c >> 16);
        }
    }
    return true;
}

}  // namespace ikaruga::host
