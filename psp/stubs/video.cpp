// Rows 1-4 of docs/diseno-psp.md: draws the Frame that dream::render decodes from the Tile
// Accelerator stream with the GE at 480x272, in through mode (GU_TRANSFORM_2D) with the
// 640x480 -> 480x272 scale done while converting the vertices. Textures come from texture_psp.h;
// the per-polygon state is read from the TSP word:
//   * shading instruction -> sceGuTexFunc: decal GU_TFX_REPLACE, modulate GU_TFX_MODULATE with the
//     vertex alpha forced to 1 (the hardware takes the alpha from the texture alone), decal alpha
//     GU_TFX_DECAL, modulate alpha GU_TFX_MODULATE. "Ignore texture alpha" selects GU_TCC_RGB,
//     whose alpha is the vertex's, and forces that to 1 where the hardware's would be the texel's.
//   * filter mode -> GU_NEAREST for point sampling, GU_LINEAR otherwise (trilinear has no mipmaps
//     here and becomes bilinear).
//   * clamp -> GU_CLAMP; a polygon whose coordinates never leave the unit square is clamped too
//     (dream::render::polygon_tiles), so bilinear filtering at its edge does not fetch the opposite
//     edge. Mirroring is done in the image (see texture_psp.cpp) and sampled with GU_REPEAT.
//   * SrcInstr/DstInstr -> sceGuBlendFunc, see ge_blend_factor. All eight PowerVR factors exist
//     on the GE; the secondary accumulation buffer (SrcSelect/DstSelect) does not and is ignored.
//   * user tile clip (PCW) -> sceGuScissor: mode 2 inside the rectangle, mode 3 as up to four
//     scissored draws around it, restored to the whole screen after (Ikaruga keeps its play field
//     out of the HUD columns this way).
// Not drawn: the offset (specular) colour, fog and colour clamp, which the GE's fixed function has
// no place for in through mode.
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
#include "texture_psp.h"

namespace ikaruga::host {

namespace {

constexpr int kWidth = 480, kHeight = 272, kStride = 512;
constexpr std::uint32_t kColourBytes = kStride * kHeight * 4;
constexpr float kScaleX = 480.0f / 640.0f, kScaleY = 272.0f / 480.0f;

// One display list per render: a handful of commands per strip.
alignas(64) std::uint32_t g_list[256 * 1024];
constexpr int kStripListBytes = 256;

// GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF: u, v, colour, then x, y, z. In through
// mode u and v are in texels of the bound image.
struct GeVertex {
    float u, v;
    std::uint32_t colour;  // 0xAABBGGRR
    float x, y, z;
};
constexpr int kVertexType = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D;

std::vector<GeVertex> g_vertices;
bool g_ready = false;
void (*g_trace)(const char*, ...) = nullptr;
bool g_enabled = true;
bool g_depth = true;
bool g_clip = true;
std::uint32_t g_front = kColourBytes;  // eDRAM offset of the buffer on screen
std::uint32_t g_back = 0;

// ISP/TSP instruction word fields (the same ones the Vulkan backend reads).
constexpr std::uint32_t isp_depth_mode(std::uint32_t isp) { return (isp >> 29) & 7u; }
constexpr bool pcw_texture(std::uint32_t pcw) { return ((pcw >> 3) & 1u) != 0; }
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

// The user tile clip of a polygon (mode 2 inside, 3 outside) as up to four GE scissor rectangles
// at 480x272, each {x, y, w, h}. A pixel is inside when its centre is, so edges round to nearest.
// Returns how many rectangles; -1 when the polygon is not clipped (full screen).
int clip_rects(const dream::render::Polygon& p, int rects[4][4]) {
    if (p.tile_clip < 2)
        return -1;
    const auto sx = [](int v) { return std::clamp(static_cast<int>(v * kScaleX + 0.5f), 0, kWidth); };
    const auto sy = [](int v) { return std::clamp(static_cast<int>(v * kScaleY + 0.5f), 0, kHeight); };
    const int x0 = sx(p.clip_x0), x1 = std::max(x0, sx(p.clip_x1));
    const int y0 = sy(p.clip_y0), y1 = std::max(y0, sy(p.clip_y1));
    int n = 0;
    const auto add = [&](int x, int y, int w, int h) {
        if (w > 0 && h > 0) {
            rects[n][0] = x; rects[n][1] = y; rects[n][2] = w; rects[n][3] = h;
            ++n;
        }
    };
    if (p.tile_clip == 2) {
        add(x0, y0, x1 - x0, y1 - y0);
    } else {
        add(0, 0, kWidth, y0);               // above
        add(0, y1, kWidth, kHeight - y1);    // below
        add(0, y0, x0, y1 - y0);             // left
        add(x1, y0, kWidth - x1, y1 - y0);   // right
    }
    return n;
}

}  // namespace

void video_set_clip(bool enabled) { g_clip = enabled; }

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
        const GeTexture* tex;
        int tfx, tcc, filter, wrap_u, wrap_v;
    };
    std::vector<Item> items;
    items.reserve(frame.polygon_count());
    const auto add_list = [&](dream::render::ListType list, bool blended, bool alpha_test) {
        for (const auto& p : frame.lists[static_cast<unsigned>(list)]) {
            float nearest = 0.0f;
            for (std::uint32_t i = 0; i < p.count; ++i)
                nearest = std::max(nearest, frame.vertices[p.first + i].z);
            items.push_back({&p, nearest, blended, alpha_test, nullptr, 0, 0, 0, 0, 0});
        }
    };
    add_list(dream::render::ListType::Opaque, false, false);
    add_list(dream::render::ListType::PunchThrough, false, true);
    const std::size_t translucent_start = items.size();
    add_list(dream::render::ListType::Translucent, true, false);
    // Translucent strips back to front: the smallest 1/w is the farthest.
    std::stable_sort(items.begin() + static_cast<std::ptrdiff_t>(translucent_start), items.end(),
                     [](const Item& a, const Item& b) { return a.depth < b.depth; });

    // Vertices converted once, per polygon, since the alpha a vertex carries and the scale of its
    // texture coordinates depend on its polygon's TSP word and texture.
    g_vertices.resize(frame.vertices.size());
    for (Item& item : items) {
        const auto& p = *item.poly;
        const std::uint32_t tsp = p.tsp;
        bool force_opaque_vertex = false;
        float us = 0.0f, vs = 0.0f;
        if (g_enabled && pcw_texture(p.pcw)) {
            const bool tiles = dream::render::polygon_tiles(frame, p);
            const bool clamp_u = dream::render::tsp_clamp_u(tsp) || !tiles;
            const bool clamp_v = dream::render::tsp_clamp_v(tsp) || !tiles;
            const bool mirror_u = !clamp_u && dream::render::tsp_flip_u(tsp);
            const bool mirror_v = !clamp_v && dream::render::tsp_flip_v(tsp);
            item.tex = texcache_get(p.tcw, tsp, mirror_u, mirror_v);
            if (item.tex) {
                const unsigned shading = dream::render::tsp_shading_instruction(tsp);
                const bool ignore_ta = dream::render::tsp_ignore_texture_alpha(tsp) != 0;
                static const int kTfx[4] = {GU_TFX_REPLACE, GU_TFX_MODULATE, GU_TFX_DECAL,
                                            GU_TFX_MODULATE};
                item.tfx = kTfx[shading & 3u];
                item.tcc = ignore_ta ? GU_TCC_RGB : GU_TCC_RGBA;
                // Decal and modulate take their alpha from the texel alone: with GU_TCC_RGBA the
                // GE multiplies it by the vertex alpha, so that is 1; with GU_TCC_RGB the alpha is
                // the vertex's, and the hardware's would be the (ignored, so 1) texel alpha.
                force_opaque_vertex = shading <= 1;
                item.filter = dream::render::tsp_filter_mode(tsp) == 0 ? GU_NEAREST : GU_LINEAR;
                item.wrap_u = clamp_u ? GU_CLAMP : GU_REPEAT;
                item.wrap_v = clamp_v ? GU_CLAMP : GU_REPEAT;
                us = item.tex->u_scale;
                vs = item.tex->v_scale;
                ++stats.textured;
            }
        }
        const bool use_alpha = dream::render::tsp_use_alpha(tsp) != 0 && !force_opaque_vertex;
        for (std::uint32_t i = 0; i < p.count; ++i) {
            const dream::render::Vertex& v = frame.vertices[p.first + i];
            GeVertex& o = g_vertices[p.first + i];
            o.u = v.u * us;
            o.v = v.v * vs;
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
    sceGuDisable(GU_TEXTURE_2D);
    const void* bound = nullptr;
    bool texturing = false;
    bool scissored = false;
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
            if (item.tex) {
                if (!texturing) {
                    sceGuEnable(GU_TEXTURE_2D);
                    texturing = true;
                }
                if (item.tex->pixels != bound) {
                    sceGuTexMode(item.tex->format, 0, 0, GU_TRUE);
                    sceGuTexImage(0, item.tex->width, item.tex->height, item.tex->width,
                                  item.tex->pixels);
                    sceGuTexFlush();
                    bound = item.tex->pixels;
                }
                sceGuTexFunc(item.tfx, item.tcc);
                sceGuTexFilter(item.filter, item.filter);
                sceGuTexWrap(item.wrap_u, item.wrap_v);
            } else if (texturing) {
                sceGuDisable(GU_TEXTURE_2D);
                texturing = false;
            }
            if (item.alpha_test)
                sceGuEnable(GU_ALPHA_TEST);
            else
                sceGuDisable(GU_ALPHA_TEST);
            int rects[4][4];
            const int nrects = g_clip ? clip_rects(p, rects) : -1;
            if (nrects < 0) {
                if (scissored) {
                    sceGuScissor(0, 0, kWidth, kHeight);
                    scissored = false;
                }
                sceGuDrawArray(GU_TRIANGLE_STRIP, kVertexType, static_cast<int>(p.count), nullptr,
                               g_vertices.data() + p.first);
            } else {
                // Mode 2 draws once in its rectangle; mode 3 once per band around it. Nothing to
                // draw when the rectangle (or its outside) is empty on screen.
                ++(p.tile_clip == 2 ? stats.clip_inside : stats.clip_outside);
                for (int r = 0; r < nrects; ++r) {
                    sceGuScissor(rects[r][0], rects[r][1], rects[r][2], rects[r][3]);
                    sceGuDrawArray(GU_TRIANGLE_STRIP, kVertexType, static_cast<int>(p.count),
                                   nullptr, g_vertices.data() + p.first);
                }
                scissored = true;
            }
            ++stats.drawn;
            if (g_trace) {
                const GeVertex& v0 = g_vertices[p.first];
                g_trace("strip list %d n %u isp %08x tsp %08x func %d mask %d colour %08x "
                        "x %.1f y %.1f z %.1f tcw %08x tex %d",
                        item.blended ? 2 : (item.alpha_test ? 4 : 0), p.count, p.isp, p.tsp,
                        ge_depth_func(isp_depth_mode(p.isp)),
                        (item.blended || isp_z_write_disable(p.isp)) ? 1 : 0, v0.colour, v0.x,
                        v0.y, v0.z, p.tcw, item.tex ? item.tex->format : -1);
            }
        }
    }
    sceGuScissor(0, 0, kWidth, kHeight);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_TEXTURE_2D);
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
    // The copy needs ~1 MB of heap, which the texture cache may be holding: the render on screen
    // is finished, so its textures can go.
    texcache_release_all();
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
