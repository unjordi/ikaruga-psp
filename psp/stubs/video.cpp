// Draws the Frame that dream::render decodes from the Tile
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
//   * fog -> two passes per fogged strip (the GE has no fog in through mode), see the fog block in
//     video_draw_frame.
// Not drawn: the offset (specular) colour and the colour clamp, which the GE's fixed function has
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
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "dream/render/display_list.h"
#include "dream/render/fog.h"
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
constexpr int kStripListBytes = 512;

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
bool g_fog = true;
bool g_hairlines = true;
std::vector<GeVertex> g_fog_vertices;  // second pass of fogged strips
// Perspective strips. Through mode interpolates u, v and colour linearly on screen; the PowerVR
// (and Flycast) interpolate them perspective-correct with 1/w. For a strip whose 1/w varies (the
// chapter-1 sky and ground reach x -400000 with 1/w near 0) the affine mapping samples almost one
// texel: the sky comes out flat. Those strips are clipped against a guard band around the screen
// (attributes interpolated with 1/w) and drawn in 3D mode with w = 1 / (1/w), so the GE corrects
// the perspective; g_persp_vertices / g_persp_fog_vertices hold them as triangle lists.
constexpr float kGuardX0 = -64.0f, kGuardX1 = 480.0f + 64.0f;
constexpr float kGuardY0 = -64.0f, kGuardY1 = 272.0f + 64.0f;
constexpr int kVertexType3d = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D;
int g_persp = 1;  // 0 off, 1 strips reaching outside the guard band, 2 every strip whose 1/w varies
std::vector<GeVertex> g_persp_vertices;
std::vector<GeVertex> g_persp_fog_vertices;
// Colour before the fog split, per vertex (fogged strips), for the perspective path to redo the
// split at the clipped vertices with their own 1/w.
std::vector<std::uint32_t> g_base_colours;
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

// Table fog factor for a 1/w, as the PC shader computes it (fog_factor in geometry.frag): index
// from the exponent and mantissa of density * 1/w, interpolated within the entry.
float fog_table_factor(const dream::render::FogSettings& fog, float inv_w) {
    const float z = std::clamp(inv_w * fog.density, 1.0f, 255.9999f);
    const float e = std::floor(std::log2(z));
    const float m = z * 16.0f / std::exp2(e) - 16.0f;
    const int i = std::clamp(static_cast<int>(std::floor(m) + e * 16.0f), 0, 127);
    const float t = m - std::floor(m);
    return (fog.factor[i] * (1.0f - t) + fog.delta[i] * t) / 255.0f;
}

std::uint32_t scale_rgb(std::uint32_t c, float k) {
    const auto ch = [&](int sh) {
        return static_cast<std::uint32_t>(std::clamp(((c >> sh) & 0xFFu) * k + 0.5f, 0.0f, 255.0f));
    };
    return (c & 0xFF000000u) | ch(0) | (ch(8) << 8) | (ch(16) << 16);
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
void video_set_fog(bool enabled) { g_fog = enabled; }
void video_set_hairlines(bool enabled) { g_hairlines = enabled; }
void video_set_perspective(int mode) { g_persp = mode; }

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

namespace {

// A clip vertex: screen x, y, depth, 1/w, and u, v and the two colours (draw and fog pass).
struct ClipVertex {
    float x, y, z, rhw;
    float u, v;
    float c[4], f[4], b[4];
};

bool outside_guard(const GeVertex& v) {
    return v.x < kGuardX0 || v.x > kGuardX1 || v.y < kGuardY0 || v.y > kGuardY1;
}

ClipVertex clip_lerp(const ClipVertex& a, const ClipVertex& b, float s) {
    // Screen-space position and depth (linear in 1/w) go linearly; the rest with 1/w.
    ClipVertex o;
    o.x = a.x + (b.x - a.x) * s;
    o.y = a.y + (b.y - a.y) * s;
    o.z = a.z + (b.z - a.z) * s;
    const float wa = a.rhw * (1.0f - s), wb = b.rhw * s;
    o.rhw = wa + wb;
    const float k = o.rhw > 0.0f ? 1.0f / o.rhw : 0.0f;
    o.u = (a.u * wa + b.u * wb) * k;
    o.v = (a.v * wa + b.v * wb) * k;
    for (int i = 0; i < 4; ++i) {
        o.c[i] = (a.c[i] * wa + b.c[i] * wb) * k;
        o.f[i] = (a.f[i] * wa + b.f[i] * wb) * k;
        o.b[i] = (a.b[i] * wa + b.b[i] * wb) * k;
    }
    return o;
}

ClipVertex to_clip(const GeVertex& g, const GeVertex& fog, std::uint32_t base, float rhw) {
    ClipVertex o;
    o.x = g.x;
    o.y = g.y;
    o.z = g.z;
    o.rhw = rhw;
    o.u = g.u;
    o.v = g.v;
    for (int i = 0; i < 4; ++i) {
        o.c[i] = static_cast<float>((g.colour >> (8 * i)) & 0xFFu);
        o.f[i] = static_cast<float>((fog.colour >> (8 * i)) & 0xFFu);
        o.b[i] = static_cast<float>((base >> (8 * i)) & 0xFFu);
    }
    return o;
}

std::uint32_t pack(const float* c) {
    std::uint32_t r = 0;
    for (int i = 0; i < 4; ++i)
        r |= static_cast<std::uint32_t>(std::clamp(c[i] + 0.5f, 0.0f, 255.0f)) << (8 * i);
    return r;
}

// How a perspective strip's fog is redone at a clipped vertex: table fog (the PowerVR computes
// it per pixel from 1/w) from that vertex's 1/w; vertex fog and no fog keep the interpolated
// colours.
struct FogRedo {
    const dream::render::FogSettings* fog = nullptr;  // null: keep the interpolated colours
    int mode = 0;            // Item::fog
    std::uint32_t colour = 0;  // fog colour
};

void redo_fog(ClipVertex& c, const FogRedo& r) {
    if (!r.fog)
        return;
    const float f = std::clamp(fog_table_factor(*r.fog, c.rhw), 0.0f, 1.0f);
    std::uint32_t o, q;
    if (r.mode == 2) {
        o = (r.colour & 0x00FFFFFFu) | (static_cast<std::uint32_t>(f * 255.0f + 0.5f) << 24);
        q = o;
    } else {
        o = scale_rgb(pack(c.b), 1.0f - f);
        q = (o & 0xFF000000u) | (scale_rgb(r.colour, f) & 0x00FFFFFFu);
    }
    for (int i = 0; i < 4; ++i) {
        c.c[i] = static_cast<float>((o >> (8 * i)) & 0xFFu);
        c.f[i] = static_cast<float>((q >> (8 * i)) & 0xFFu);
    }
}

// 3D-mode vertex: clip coordinates (ndc * w, w) with the identity view and model matrices; the
// projection matrix (set per frame) turns z = w into the depth the through path would write.
GeVertex to_3d(const ClipVertex& c, bool fog_pass) {
    const float w = 1.0f / c.rhw;
    GeVertex o;
    o.u = c.u;
    o.v = c.v;
    o.colour = pack(fog_pass ? c.f : c.c);
    o.x = (c.x / 240.0f - 1.0f) * w;
    o.y = (1.0f - c.y / 136.0f) * w;
    o.z = w;
    return o;
}

// Clips the strip's triangles against the guard band and appends them as 3D triangle lists to
// g_persp_vertices and g_persp_fog_vertices.
void clip_strip(const GeVertex* v, const GeVertex* fog, const std::uint32_t* base,
                const dream::render::Vertex* src, std::uint32_t n, const FogRedo& redo) {
    for (std::uint32_t t = 0; t + 2 < n; ++t) {
        ClipVertex poly[12], next[12];
        int count = 3;
        for (int k = 0; k < 3; ++k)
            poly[k] = to_clip(v[t + k], fog[t + k], base[t + k], std::max(src[t + k].z, 1e-12f));
        for (int plane = 0; plane < 4 && count > 0; ++plane) {
            const auto dist = [plane](const ClipVertex& q) {
                switch (plane) {
                case 0: return q.x - kGuardX0;
                case 1: return kGuardX1 - q.x;
                case 2: return q.y - kGuardY0;
                default: return kGuardY1 - q.y;
                }
            };
            int out = 0;
            for (int k = 0; k < count; ++k) {
                const ClipVertex& a = poly[k];
                const ClipVertex& b = poly[(k + 1) % count];
                const float da = dist(a), db = dist(b);
                if (da >= 0.0f)
                    next[out++] = a;
                if ((da >= 0.0f) != (db >= 0.0f))
                    next[out++] = clip_lerp(a, b, da / (da - db));
            }
            count = out;
            for (int k = 0; k < count; ++k)
                poly[k] = next[k];
        }
        for (int k = 0; k < count; ++k)
            redo_fog(poly[k], redo);
        for (int k = 1; k + 1 < count; ++k) {
            for (int j : {0, k, k + 1}) {
                g_persp_vertices.push_back(to_3d(poly[j], false));
                g_persp_fog_vertices.push_back(to_3d(poly[j], true));
            }
        }
    }
}

}  // namespace

VideoStats video_draw_frame(const dream::render::Frame& frame, float alpha_ref,
                            const dream::render::FogSettings* fog) {
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
        // 0 no fog; 1 drawn scaled by (1 - f) and then f * fog colour added in a second pass
        // (g_fog_vertices); 2 table-alpha fog, which replaces the colour with the fog colour.
        int fog;
        // A kRect copy: index in `soft_rects` of the integer screen rectangle it covers 1:1, or -1.
        // (An index: the translucent items are sorted, and a fat item makes the sort slower.)
        int rect;
        // Drawn in 3D mode (perspective): first vertex in g_persp_vertices and count, or -1.
        int persp_first = -1;
        int persp_count = 0;
    };
    std::vector<SoftRect> soft_rects;
    std::vector<Item> items;
    items.reserve(frame.polygon_count());
    const auto add_list = [&](dream::render::ListType list, bool blended, bool alpha_test) {
        for (const auto& p : frame.lists[static_cast<unsigned>(list)]) {
            float nearest = 0.0f;
            for (std::uint32_t i = 0; i < p.count; ++i)
                nearest = std::max(nearest, frame.vertices[p.first + i].z);
            items.push_back({&p, nearest, blended, alpha_test, nullptr, 0, 0, 0, 0, 0, 0, -1});
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
    g_fog_vertices.resize(frame.vertices.size());
    g_base_colours.assign(frame.vertices.size(), 0u);
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
            // Minified bilinear quads (more than 1.5 texels per screen pixel on an axis) take the
            // cache's prefiltered copy: see soften() in texture_psp.cpp.
            SoftSpec soft;
            bool soft_used = false;
            // Only small 1-bit-alpha images (ARGB1555, at most 16384 texels): the text the game
            // composes in VRAM. Prefiltered copies of the 3D textures would double the cache.
            const float tw = static_cast<float>(8u << (tsp >> 3 & 7u));
            const float th = static_cast<float>(8u << (tsp & 7u));
            if (g_hairlines && p.count == 4 && dream::render::tsp_filter_mode(tsp) != 0 &&
                ((p.tcw >> 27) & 7u) == 0 && tw * th <= 16384.0f) {
                const dream::render::Vertex* q = &frame.vertices[p.first];
                bool minified = false;
                for (int k : {1, 2}) {
                    const float dx = std::fabs(q[k].x - q[0].x) * kScaleX;
                    const float dy = std::fabs(q[k].y - q[0].y) * kScaleY;
                    const float du = std::fabs(q[k].u - q[0].u) * tw;
                    const float dv = std::fabs(q[k].v - q[0].v) * th;
                    if ((dx >= 1.0f && du / dx > 1.5f) || (dy >= 1.0f && dv / dy > 1.5f))
                        minified = true;
                }
                if (minified) {
                    soft.mode = SoftSpec::kTent;
                    soft_used = true;
                    // An upright quad (two x and two y, u following x and v following y) is
                    // resampled onto its screen rectangle instead.
                    // Rotated quads (the particles) have no neighbour on q[0]'s x or y: skip the rest.
                    const bool upright = std::fabs(q[0].x - q[1].x) < 0.01f || std::fabs(q[0].x - q[2].x) < 0.01f ||
                                         std::fabs(q[0].y - q[1].y) < 0.01f || std::fabs(q[0].y - q[2].y) < 0.01f;
                    float xl = q[0].x, xr = q[0].x, yt = q[0].y, yb = q[0].y;
                    for (int k = 1; k < 4 && upright; ++k) {
                        xl = std::min(xl, q[k].x), xr = std::max(xr, q[k].x);
                        yt = std::min(yt, q[k].y), yb = std::max(yb, q[k].y);
                    }
                    const dream::render::Vertex *tl = nullptr, *br = nullptr;
                    int corners = 0;
                    for (int k = 0; k < 4 && upright; ++k) {
                        const bool l = std::fabs(q[k].x - xl) < 0.01f, r = std::fabs(q[k].x - xr) < 0.01f;
                        const bool t = std::fabs(q[k].y - yt) < 0.01f, b = std::fabs(q[k].y - yb) < 0.01f;
                        corners += (l || r) && (t || b);
                        if (l && t) tl = &q[k];
                        if (r && b) br = &q[k];
                    }
                    const auto quarter = [](float v) {
                        return static_cast<std::int16_t>(std::lround(v * 4.0f));
                    };
                    if (upright && corners == 4 && tl && br && xr - xl > 1.0f && yb - yt > 1.0f &&
                        xr * kScaleX - xl * kScaleX < 255.0f && yb - yt < 480.0f &&
                        br->u > tl->u && br->v > tl->v && !mirror_u && !mirror_v &&
                        tl->u >= 0.0f && br->u <= 1.0f && tl->v >= 0.0f && br->v <= 1.0f) {
                        soft.mode = SoftSpec::kRect;
                        soft.x0 = quarter(xl), soft.y0 = quarter(yt);
                        soft.x1 = quarter(xr), soft.y1 = quarter(yb);
                        soft.u0 = quarter(tl->u * tw), soft.v0 = quarter(tl->v * th);
                        soft.u1 = quarter(br->u * tw), soft.v1 = quarter(br->v * th);
                        item.rect = static_cast<int>(soft_rects.size());
                        soft_rects.push_back(soft_rect(soft));
                    }
                }
            }
            item.tex = texcache_get(p.tcw, tsp, mirror_u, mirror_v, soft_used ? &soft : nullptr);
            if (!item.tex) item.rect = -1;
            if (soft_used && item.tex)
                ++stats.softened;
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
        // Fog (TSP bits 22-23): 0 table, 1 per vertex (offset alpha, only with an offset colour),
        // 2 none, 3 table on alpha only. The GE has no fog in through mode, so the PC shader's
        // mix(colour, fog, f) is split: this pass draws colour * (1 - f), the second adds f * fog.
        const unsigned fog_control = dream::render::tsp_fog_control(tsp);
        const bool offset = ((p.pcw >> 2) & 1u) != 0;
        if (g_fog && fog && (fog_control == 0 || fog_control == 3 || (fog_control == 1 && offset))) {
            item.fog = fog_control == 3 ? 2 : 1;
            ++stats.fogged;
            if (item.fog == 2)
                item.tex = nullptr;  // colour and alpha both come from the fog
            else if (item.tex && item.tfx == GU_TFX_REPLACE)
                item.tfx = GU_TFX_MODULATE;  // decal times a grey (1 - f) vertex
        }
        const std::uint32_t fog_colour =
            fog ? (fog_control == 1 ? fog->vertex_colour : fog->table_colour) : 0;
        const bool replace_to_modulate = item.fog == 1 && item.tex &&
            dream::render::tsp_shading_instruction(tsp) == 0;
        const bool on_rect = item.rect >= 0;
        const SoftRect rect = on_rect ? soft_rects[static_cast<std::size_t>(item.rect)] : SoftRect();
        for (std::uint32_t i = 0; i < p.count; ++i) {
            const dream::render::Vertex& v = frame.vertices[p.first + i];
            GeVertex& o = g_vertices[p.first + i];
            o.u = v.u * us;
            o.v = v.v * vs;
            o.colour = argb_to_abgr(v.base, use_alpha);
            o.x = v.x * kScaleX;
            o.y = v.y * kScaleY;
            if (on_rect) {  // the copy is the rectangle's pixels: draw it over them 1:1
                const bool right = v.x * kScaleX > static_cast<float>(rect.x) + 0.5f * static_cast<float>(rect.w);
                const bool bottom = v.y * kScaleY > static_cast<float>(rect.y) + 0.5f * static_cast<float>(rect.h);
                o.x = static_cast<float>(rect.x + (right ? rect.w : 0));
                o.y = static_cast<float>(rect.y + (bottom ? rect.h : 0));
                o.u = right ? static_cast<float>(rect.w) : 0.0f;
                o.v = bottom ? static_cast<float>(rect.h) : 0.0f;
            }
            o.z = std::clamp(zbias + (v.z - zmin) * zscale, 1.0f, 65535.0f);
            if (item.fog) {
                const float f = std::clamp(fog_control == 1
                    ? static_cast<float>(v.offset >> 24) / 255.0f
                    : fog_table_factor(*fog, v.z), 0.0f, 1.0f);
                GeVertex& q = g_fog_vertices[p.first + i];
                q = o;
                if (item.fog == 2) {
                    o.colour = (fog_colour & 0x00FFFFFFu) |
                        (static_cast<std::uint32_t>(f * 255.0f + 0.5f) << 24);
                } else {
                    const std::uint32_t base = replace_to_modulate ? (o.colour | 0x00FFFFFFu) : o.colour;
                    g_base_colours[p.first + i] = base;
                    o.colour = scale_rgb(base, 1.0f - f);
                    q.colour = (o.colour & 0xFF000000u) | (scale_rgb(fog_colour, f) & 0x00FFFFFFu);
                }
            }
        }
    }
    // Hairlines: the HUD draws its lines (the chain boxes, the frames) as untextured quads one
    // pixel thick at 640x480. Scaled to 480x272 they are 0.57-0.75 px thick and the GE, which
    // fills only pixels whose centre is inside, drops most of them (dots, or nothing). A quad
    // (parallelogram strip) thinner than a pixel on screen is widened to one pixel about its
    // centre line, which is what the hardware's 640x480 image box-filtered down shows.
    if (g_hairlines) {
        for (const Item& item : items) {
            const auto& p = *item.poly;
            if (p.count != 4 || item.tex)
                continue;
            GeVertex* q = &g_vertices[p.first];
            const float ax = q[1].x - q[0].x, ay = q[1].y - q[0].y;
            const float bx = q[2].x - q[0].x, by = q[2].y - q[0].y;
            if (std::fabs(q[3].x - (q[1].x + bx)) > 0.25f || std::fabs(q[3].y - (q[1].y + by)) > 0.25f)
                continue;  // not a parallelogram
            const bool a_long = ax * ax + ay * ay >= bx * bx + by * by;
            const float lx = a_long ? ax : bx, ly = a_long ? ay : by;
            const float sx = a_long ? bx : ax, sy = a_long ? by : ay;
            const float len = std::sqrt(lx * lx + ly * ly);
            if (len < 1.0f)
                continue;
            const float nx = -ly / len, ny = lx / len;
            const float d = sx * nx + sy * ny;
            const float t = std::fabs(d);
            if (t >= 1.0f)
                continue;
            const float e = (1.0f - t) * 0.5f * (d < 0.0f ? -1.0f : 1.0f);
            // q[0] and the far end of the long edge are one side; the other two the other side.
            const int far = a_long ? 1 : 2, other = a_long ? 2 : 1;
            for (int i : {0, far}) { q[i].x -= e * nx; q[i].y -= e * ny; }
            for (int i : {other, 3}) { q[i].x += e * nx; q[i].y += e * ny; }
            if (item.fog == 1)
                for (int i = 0; i < 4; ++i) {
                    g_fog_vertices[p.first + i].x = q[i].x;
                    g_fog_vertices[p.first + i].y = q[i].y;
                }
            ++stats.hairlines;
        }
    }
    // Minified point-sampled sprites: the hardware draws at 640x480 and the image is seen box-
    // filtered to 480x272; nearest sampling straight at 480x272 skips texel rows and columns (the
    // HUD's "CHAIN STATUS", 32 texel rows in 18 pixels, loses its strokes). A point-sampled quad
    // with more than 1.25 texels per screen pixel along either axis is sampled bilinearly.
    if (g_hairlines) {
        for (Item& item : items) {
            const auto& p = *item.poly;
            if (p.count != 4 || !item.tex || item.filter != GU_NEAREST)
                continue;
            const GeVertex* q = &g_vertices[p.first];
            float worst = 0.0f;
            for (int k : {1, 2}) {
                const float dx = std::fabs(q[k].x - q[0].x), dy = std::fabs(q[k].y - q[0].y);
                const float du = std::fabs(q[k].u - q[0].u), dv = std::fabs(q[k].v - q[0].v);
                if (dx >= 1.0f) worst = std::max(worst, du / dx);
                if (dy >= 1.0f) worst = std::max(worst, dv / dy);
            }
            if (worst > 1.25f) {
                item.filter = GU_LINEAR;
                ++stats.minified;
            }
        }
    }
    // Perspective strips (see kGuardX0): clipped and converted to 3D mode.
    g_persp_vertices.clear();
    g_persp_fog_vertices.clear();
    if (g_persp) {
        for (Item& item : items) {
            const auto& p = *item.poly;
            if (item.rect >= 0)
                continue;
            bool pick = false;
            float lo = frame.vertices[p.first].z, hi = lo;
            for (std::uint32_t i = 0; i < p.count; ++i) {
                pick = pick || outside_guard(g_vertices[p.first + i]);
                lo = std::min(lo, frame.vertices[p.first + i].z);
                hi = std::max(hi, frame.vertices[p.first + i].z);
            }
            if (g_persp == 2 && item.tex && hi > lo * 1.01f)
                pick = true;
            if (!pick || !(lo > 0.0f))
                continue;
            item.persp_first = static_cast<int>(g_persp_vertices.size());
            FogRedo redo;
            const unsigned fog_control = dream::render::tsp_fog_control(p.tsp);
            if (item.fog && fog_control != 1) {
                redo.fog = fog;
                redo.mode = item.fog;
                redo.colour = fog->table_colour;
            }
            clip_strip(&g_vertices[p.first], &g_fog_vertices[p.first], &g_base_colours[p.first],
                       &frame.vertices[p.first], p.count, redo);
            item.persp_count = static_cast<int>(g_persp_vertices.size()) - item.persp_first;
            ++stats.perspective;
        }
        sceKernelDcacheWritebackRange(g_persp_vertices.data(),
            static_cast<unsigned>(g_persp_vertices.size() * sizeof(GeVertex)));
        sceKernelDcacheWritebackRange(g_persp_fog_vertices.data(),
            static_cast<unsigned>(g_persp_fog_vertices.size() * sizeof(GeVertex)));
    }
    sceKernelDcacheWritebackRange(g_vertices.data(),
                                  static_cast<unsigned>(g_vertices.size() * sizeof(GeVertex)));
    sceKernelDcacheWritebackRange(g_fog_vertices.data(),
                                  static_cast<unsigned>(g_fog_vertices.size() * sizeof(GeVertex)));

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
    if (g_persp) {
        // 3D mode for the perspective strips: clip = (x, y, A w + B, w), so z / w = A + B / w is
        // the ndc depth that the viewport (depth range 65535..0: 32767 - 32768 z) turns into the
        // through path's zbias + (1/w - zmin) * zscale.
        const float a = (32767.0f - zbias + zmin * zscale) / 32768.0f;
        const float b = -zscale / 32768.0f;
        ScePspFMatrix4 m = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, a, 1}, {0, 0, b, 0}};
        sceGuSetMatrix(GU_PROJECTION, &m);
        ScePspFMatrix4 id = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
        sceGuSetMatrix(GU_VIEW, &id);
        sceGuSetMatrix(GU_MODEL, &id);
        sceGuTexOffset(0.0f, 0.0f);
    }
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
            if (nrects >= 0)
                ++(p.tile_clip == 2 ? stats.clip_inside : stats.clip_outside);
            // Mode 2 draws once in its rectangle; mode 3 once per band around it. Nothing to
            // draw when the rectangle (or its outside) is empty on screen.
            const bool persp = item.persp_first >= 0;
            if (persp && item.persp_count == 0)
                continue;  // wholly outside the guard band
            const int prim = persp ? GU_TRIANGLES : GU_TRIANGLE_STRIP;
            const int vtype = persp ? kVertexType3d : kVertexType;
            const int nverts = persp ? item.persp_count : static_cast<int>(p.count);
            if (persp && item.tex)  // 3D mode takes u, v normalised to the image
                sceGuTexScale(1.0f / static_cast<float>(item.tex->width),
                              1.0f / static_cast<float>(item.tex->height));
            const auto draw = [&](bool fog_pass) {
                const GeVertex* v = persp
                    ? (fog_pass ? g_persp_fog_vertices : g_persp_vertices).data() + item.persp_first
                    : (fog_pass ? g_fog_vertices : g_vertices).data() + p.first;
                if (nrects < 0) {
                    if (scissored) {
                        sceGuScissor(0, 0, kWidth, kHeight);
                        scissored = false;
                    }
                    sceGuDrawArray(prim, vtype, nverts, nullptr, v);
                    return;
                }
                for (int r = 0; r < nrects; ++r) {
                    sceGuScissor(rects[r][0], rects[r][1], rects[r][2], rects[r][3]);
                    sceGuDrawArray(prim, vtype, nverts, nullptr, v);
                }
                scissored = true;
            };
            draw(false);
            if (item.fog == 1) {
                // Second pass: f * fog colour added where the first pass landed. GU_TFX_BLEND
                // with the vertex and environment colours both f * fog gives that colour with the
                // texel's alpha, so the texture still gates it (punch-through, translucency).
                // Opaque strips add on equal depth; translucent ones repeat their own source factor
                // (only alpha or one have a direct equivalent; others get no fog).
                const unsigned src = dream::render::tsp_src_instr(p.tsp);
                if (!item.blended || src == 1 || src == 4) {
                    // A perspective strip spans many fog factors, and the environment colour is
                    // one: when the texel's alpha does not matter (opaque, no alpha test) the
                    // pass goes untextured and the vertices' f * fog is exact.
                    const bool plain = persp && item.tex && !item.blended && !item.alpha_test;
                    if (plain)
                        sceGuDisable(GU_TEXTURE_2D);
                    if (item.tex && !plain) {
                        sceGuTexFunc(GU_TFX_BLEND, item.tfx == GU_TFX_DECAL ? GU_TCC_RGB : item.tcc);
                        sceGuTexEnvColor(g_fog_vertices[p.first].colour & 0x00FFFFFFu);
                    }
                    sceGuEnable(GU_BLEND);
                    if (item.blended && src == 4)
                        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFF);
                    else
                        sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF);
                    if (!item.blended)
                        sceGuDepthFunc(GU_EQUAL);
                    sceGuDepthMask(GU_TRUE);
                    draw(true);
                    if (plain)
                        sceGuEnable(GU_TEXTURE_2D);
                    else if (item.tex)
                        sceGuTexFunc(item.tfx, item.tcc);
                }
            }
            ++stats.drawn;
            if (g_trace) {
                const GeVertex& v0 = g_vertices[p.first];
                const SoftRect rect = item.rect >= 0 ? soft_rects[static_cast<std::size_t>(item.rect)] : SoftRect();
                g_trace("strip list %d n %u isp %08x tsp %08x func %d mask %d colour %08x "
                        "x %.1f y %.1f z %.1f tcw %08x tex %d rect %d,%d %dx%d",
                        item.blended ? 2 : (item.alpha_test ? 4 : 0), p.count, p.isp, p.tsp,
                        ge_depth_func(isp_depth_mode(p.isp)),
                        (item.blended || isp_z_write_disable(p.isp)) ? 1 : 0, v0.colour, v0.x,
                        v0.y, v0.z, p.tcw, item.tex ? item.tex->format : -1, rect.x, rect.y,
                        rect.w, rect.h);
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
