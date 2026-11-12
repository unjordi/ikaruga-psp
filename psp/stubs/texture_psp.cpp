// PowerVR2 texture cache for the GE (see texture_psp.h for the contract).
//
// Format choice, by the PowerVR pixel format (for palette textures, by the palette's own format):
//   ARGB1555 -> GU_PSM_5551, RGB565 -> GU_PSM_5650, ARGB4444 -> GU_PSM_4444: the same bits, so the
//   round trip through the decoder's RGBA8888 is exact.  YUV422, bump maps and ARGB8888 palettes
//   -> GU_PSM_8888, since no 16-bit GE format holds them without loss.
// Palette textures are expanded rather than sent as a CLUT: the title's are small, and expanding
// keeps one code path and lets a palette change invalidate just the textures that read the
// entries that changed.
//
// The GE samples at most 512x512. A larger image (a 1024 side, or a 512 side doubled for
// mirroring) is reduced by a box filter along that axis, and the polygon's texture coordinates are
// scaled with it through GeTexture::u_scale/v_scale.
//
// Mirroring (the TSP flip bits without clamp) has no GE wrap mode: the image is stored as the
// texture followed by its mirror image along that axis, and sampled with GU_REPEAT, which repeats
// the pair and so mirrors every other copy.
#include "texture_psp.h"

#include <malloc.h>
#include <pspkernel.h>
#include <pspgu.h>

#include <algorithm>
#include <new>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "dream/render/texture.h"
#include "host.h"

namespace ikaruga::host {

namespace {

using dream::render::PixelFormat;
using dream::render::TextureInfo;

constexpr int kMaxSide = 512;

struct Entry {
    TextureInfo info;
    GeTexture tex;
    void* memory = nullptr;
    std::uint32_t bytes = 0;
    std::uint32_t source_hash = 0, palette_hash = 0;
    std::uint32_t checked = 0;  // the render the hashes were last compared in
    std::uint32_t epoch = 0;    // the VRAM write epoch source_hash is valid for
    std::uint32_t used = 0;     // the last render that asked for it
    bool failed = false;
    SoftSpec soft;  // prefiltered copy for minified bilinear quads: see soften()
};

bool g_enabled = true;
std::uint32_t g_budget = 2560u << 10;
std::unordered_map<std::uint64_t, Entry> g_entries;
TextureCacheStats g_stats;
std::uint32_t g_render = 0;
const std::uint8_t* g_vram = nullptr;
std::size_t g_vram_size = 0;
unsigned g_palette_format = 0;
std::uint32_t g_palette[1024];
const std::uint32_t* g_page_epoch = nullptr;
std::uint32_t g_epoch = 0;
constexpr unsigned kPageShift = 12;

std::uint32_t hash_bytes(const std::uint8_t* p, std::size_t n, std::uint32_t h) {
    std::size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        std::uint32_t w;
        std::memcpy(&w, p + i, 4);
        h = ((h << 5) | (h >> 27)) ^ w;
        h *= 0x9E3779B1u;
    }
    for (; i < n; ++i)
        h = (h ^ p[i]) * 0x01000193u;
    return h ^ (h >> 15);
}

// Bytes of video memory the texture reads, clamped to video memory.
std::size_t source_span(const TextureInfo& info) {
    std::size_t bytes = info.size_bytes();
    if (bytes == 0)
        bytes = static_cast<std::size_t>(info.width) * info.height * 2;
    if (info.address >= g_vram_size)
        return 0;
    return std::min<std::size_t>(bytes, g_vram_size - info.address);
}

std::uint32_t source_hash(const TextureInfo& info) {
    return hash_bytes(g_vram + info.address, source_span(info), 0x811C9DC5u);
}

// True when a page the texture reads was written after `since`, or when pages are not tracked.
bool source_dirty(const TextureInfo& info, std::uint32_t since) {
    if (!g_page_epoch)
        return true;
    const std::size_t span = source_span(info);
    if (span == 0)
        return false;
    const std::size_t first = info.address >> kPageShift;
    const std::size_t last = (info.address + span - 1) >> kPageShift;
    for (std::size_t p = first; p <= last; ++p)
        if (g_page_epoch[p] > since)
            return true;
    return false;
}

std::uint32_t palette_hash(const TextureInfo& info) {
    if (!info.indexed())
        return 0;
    const std::uint32_t n = info.format == PixelFormat::Palette4 ? 16u : 256u;
    const std::uint32_t first = std::min<std::uint32_t>(info.palette_base, 1024u);
    const std::uint32_t count = std::min<std::uint32_t>(n, 1024u - first);
    return hash_bytes(reinterpret_cast<const std::uint8_t*>(g_palette + first), count * 4u,
                      0x01000193u);
}

int ge_format(const TextureInfo& info) {
    PixelFormat f = info.format;
    if (info.indexed()) {
        switch (g_palette_format & 3u) {
            case 0: return GU_PSM_5551;
            case 1: return GU_PSM_5650;
            case 2: return GU_PSM_4444;
            default: return GU_PSM_8888;
        }
    }
    switch (f) {
        case PixelFormat::Argb1555: return GU_PSM_5551;
        case PixelFormat::Rgb565: return GU_PSM_5650;
        case PixelFormat::Argb4444: return GU_PSM_4444;
        default: return GU_PSM_8888;
    }
}

// RGBA8888 (0xAABBGGRR, the decoder's and the GE's byte order) to the GE format.
std::uint32_t pack(std::uint32_t c, int format) {
    const std::uint32_t r = c & 0xFFu, g = (c >> 8) & 0xFFu, b = (c >> 16) & 0xFFu, a = c >> 24;
    switch (format) {
        case GU_PSM_5551: return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | ((a >> 7) << 15);
        case GU_PSM_5650: return (r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11);
        case GU_PSM_4444: return (r >> 4) | ((g >> 4) << 4) | ((b >> 4) << 8) | ((a >> 4) << 12);
        default: return c;
    }
}

void release(Entry& e) {
    if (e.memory) {
        free(e.memory);
        g_stats.bytes -= e.bytes;
    }
    e.memory = nullptr;
    e.bytes = 0;
    e.tex = GeTexture{};
}

// Frees least recently used images that the current render has not handed out until `bytes` more
// fit. False when they cannot be made to fit.
bool make_room(std::uint32_t bytes) {
    if (bytes > g_budget)
        return false;
    while (g_stats.bytes + bytes > g_budget) {
        Entry* oldest = nullptr;
        for (auto& [key, e] : g_entries)
            if (e.memory && e.used != g_render && (!oldest || e.used < oldest->used))
                oldest = &e;
        if (!oldest)
            return false;
        release(*oldest);
        ++g_stats.evicted;
        // The bookkeeping goes too, so the map stays as small as the images it holds.
        for (auto it = g_entries.begin(); it != g_entries.end(); ++it)
            if (&it->second == oldest) {
                g_entries.erase(it);
                break;
            }
    }
    return true;
}

// The GE format of an entry: a soft copy of a 1-bit-alpha image needs the in-between alphas.
int entry_format(const Entry& e) {
    const int f = ge_format(e.info);
    return e.soft.mode != SoftSpec::kNone && f == GU_PSM_5551 ? GU_PSM_4444 : f;
}

// Minified bilinear quads: the hardware draws at 640x480 and the screen seen is that image box-
// filtered to 480x272, so a texture shrunk on screen reaches it averaged over neighbouring texels;
// the GE at 480x272 takes only the 4 nearest (thin strokes come out broken and too sharp: the menu
// legend, 2.2 texels per pixel vertically).
// kTent: the image through a 1-2-1 tent in both axes, weighted by alpha (the colour of
// transparent texels does not bleed in).
void soften_tent(std::vector<std::uint32_t>& rgba, int w, int h) {
    std::vector<float> pm(static_cast<std::size_t>(w) * h * 4), tmp(pm.size());
    for (std::size_t i = 0; i < rgba.size(); ++i) {
        const std::uint32_t c = rgba[i];
        const float a = static_cast<float>(c >> 24);
        for (int k = 0; k < 3; ++k) pm[i * 4 + k] = static_cast<float>((c >> (8 * k)) & 0xFFu) * a;
        pm[i * 4 + 3] = a;
    }
    const auto pass = [&](const std::vector<float>& in, std::vector<float>& out, bool horiz) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const int x0 = horiz ? std::max(x - 1, 0) : x, x1 = horiz ? std::min(x + 1, w - 1) : x;
                const int y0 = horiz ? y : std::max(y - 1, 0), y1 = horiz ? y : std::min(y + 1, h - 1);
                const std::size_t c = (static_cast<std::size_t>(y) * w + x) * 4;
                const std::size_t a = (static_cast<std::size_t>(y0) * w + x0) * 4;
                const std::size_t b = (static_cast<std::size_t>(y1) * w + x1) * 4;
                for (int k = 0; k < 4; ++k)
                    out[c + k] = 0.25f * in[a + k] + 0.5f * in[c + k] + 0.25f * in[b + k];
            }
    };
    pass(pm, tmp, true);
    pass(tmp, pm, false);
    for (std::size_t i = 0; i < rgba.size(); ++i) {
        const float a = pm[i * 4 + 3];
        std::uint32_t c = static_cast<std::uint32_t>(a + 0.5f) << 24;
        if (a > 0.0f)
            for (int k = 0; k < 3; ++k)
                c |= static_cast<std::uint32_t>(std::min(pm[i * 4 + k] / a + 0.5f, 255.0f)) << (8 * k);
        rgba[i] = c;
    }
}

}  // namespace

SoftRect soft_rect(const SoftSpec& s) {
    const float x0 = static_cast<float>(s.x0) * 0.25f * kSoftScaleX;
    const float x1 = static_cast<float>(s.x1) * 0.25f * kSoftScaleX;
    const float y0 = static_cast<float>(s.y0) * 0.25f * kSoftScaleY;
    const float y1 = static_cast<float>(s.y1) * 0.25f * kSoftScaleY;
    SoftRect r;
    r.x = static_cast<int>(std::floor(x0)), r.y = static_cast<int>(std::floor(y0));
    r.w = static_cast<int>(std::ceil(x1)) - r.x, r.h = static_cast<int>(std::ceil(y1)) - r.y;
    return r;
}

namespace {

// kRect: the w x h texture seen through the quad `s`, as the screen pixels of soft_rect(s). Each
// pixel of the 640x480 render whose centre is inside the quad takes the bilinear sample of the
// texture at that centre; each screen pixel is the area average of the 640x480 pixels it covers
// (weighted by alpha for the colour; the part of the pixel outside the quad is transparent).
void soften_rect(std::vector<std::uint32_t>& rgba, int w, int h, const SoftSpec& s) {
    const SoftRect r = soft_rect(s);
    const float qx0 = static_cast<float>(s.x0) * 0.25f, qx1 = static_cast<float>(s.x1) * 0.25f;
    const float qy0 = static_cast<float>(s.y0) * 0.25f, qy1 = static_cast<float>(s.y1) * 0.25f;
    const float u0 = static_cast<float>(s.u0) * 0.25f, u1 = static_cast<float>(s.u1) * 0.25f;
    const float v0 = static_cast<float>(s.v0) * 0.25f, v1 = static_cast<float>(s.v1) * 0.25f;
    // Premultiplied bilinear sample at texel coordinates (u, v), clamped at the edges.
    const auto sample = [&](float u, float v, float out[4]) {
        const float fu = u - 0.5f, fv = v - 0.5f;
        const int iu = static_cast<int>(std::floor(fu)), iv = static_cast<int>(std::floor(fv));
        const float tu = fu - static_cast<float>(iu), tv = fv - static_cast<float>(iv);
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i) {
                const int x = std::min(std::max(iu + i, 0), w - 1);
                const int y = std::min(std::max(iv + j, 0), h - 1);
                const float f = (i ? tu : 1.0f - tu) * (j ? tv : 1.0f - tv);
                const std::uint32_t c = rgba[static_cast<std::size_t>(y) * w + x];
                const float a = static_cast<float>(c >> 24);
                for (int k = 0; k < 3; ++k) out[k] += f * a * static_cast<float>((c >> (8 * k)) & 0xFFu);
                out[3] += f * a;
            }
    };
    std::vector<std::uint32_t> img(static_cast<std::size_t>(r.w) * r.h);
    const float inv_x = 1.0f / kSoftScaleX, inv_y = 1.0f / kSoftScaleY;  // 640x480 pixels per pixel
    for (int Y = 0; Y < r.h; ++Y) {
        const float ya = static_cast<float>(r.y + Y) * inv_y, yb = ya + inv_y;
        for (int X = 0; X < r.w; ++X) {
            const float xa = static_cast<float>(r.x + X) * inv_x, xb = xa + inv_x;
            float acc[4] = {};
            for (int j = static_cast<int>(std::floor(ya)); static_cast<float>(j) < yb; ++j) {
                const float cy = static_cast<float>(j) + 0.5f;
                if (cy < qy0 || cy >= qy1) continue;
                const float wy = std::min(yb, static_cast<float>(j + 1)) - std::max(ya, static_cast<float>(j));
                const float v = v0 + (cy - qy0) / (qy1 - qy0) * (v1 - v0);
                for (int i = static_cast<int>(std::floor(xa)); static_cast<float>(i) < xb; ++i) {
                    const float cx = static_cast<float>(i) + 0.5f;
                    if (cx < qx0 || cx >= qx1) continue;
                    const float wx = std::min(xb, static_cast<float>(i + 1)) - std::max(xa, static_cast<float>(i));
                    const float u = u0 + (cx - qx0) / (qx1 - qx0) * (u1 - u0);
                    float px[4];
                    sample(u, v, px);
                    for (int k = 0; k < 4; ++k) acc[k] += wx * wy * px[k];
                }
            }
            std::uint32_t c = 0;
            const float area = inv_x * inv_y;
            if (acc[3] > 0.0f) {
                c = static_cast<std::uint32_t>(std::min(acc[3] / area + 0.5f, 255.0f)) << 24;
                for (int k = 0; k < 3; ++k)
                    c |= static_cast<std::uint32_t>(std::min(acc[k] / acc[3] + 0.5f, 255.0f)) << (8 * k);
            }
            img[static_cast<std::size_t>(Y) * r.w + X] = c;
        }
    }
    rgba.swap(img);
}

// Decodes, mirrors, reduces, packs and swizzles `e.info` into a new image.
bool build(Entry& e, bool mirror_u, bool mirror_v) {
    const std::uint64_t t0 = clock_us();
    // Room first: a request that cannot fit is refused before paying for the decode.
    {
        int w = static_cast<int>(e.info.width) * (mirror_u ? 2 : 1);
        int h = static_cast<int>(e.info.height) * (mirror_v ? 2 : 1);
        while (w > kMaxSide) w /= 2;
        while (h > kMaxSide) h /= 2;
        const std::uint32_t need =
            static_cast<std::uint32_t>(w * h * (entry_format(e) == GU_PSM_8888 ? 4 : 2));
        if (!make_room(need)) {
            ++g_stats.over_budget, ++g_stats.render_over_budget;
            return false;
        }
    }
    // The decoder's RGBA8888 copy is temporary but large (2 MB for 512x1024), and the heap has
    // little to spare: when it cannot be had, images the current render has not handed out are
    // dropped and the decode is tried once more; failing that, the polygon draws untextured.
    std::vector<std::uint32_t> rgba;
    bool decoded = false;
    for (int attempt = 0; attempt < 2 && !decoded; ++attempt) {
        try {
            if (!dream::render::decode_texture(e.info, g_vram, g_vram_size, g_palette, rgba)) {
                ++g_stats.failed;
                e.failed = true;
                return false;
            }
            decoded = true;
        } catch (const std::bad_alloc&) {
            rgba = std::vector<std::uint32_t>();
            if (attempt == 0)
                make_room(g_budget);
        }
    }
    if (!decoded) {
        ++g_stats.over_budget, ++g_stats.render_over_budget;
        return false;
    }
    int w = static_cast<int>(e.info.width), h = static_cast<int>(e.info.height);
    const bool shrunk = e.soft.mode == SoftSpec::kRect && !mirror_u && !mirror_v;
    if (shrunk) {
        const SoftRect r = soft_rect(e.soft);
        soften_rect(rgba, w, h, e.soft);
        w = r.w, h = r.h;
    } else if (e.soft.mode == SoftSpec::kTent) {
        soften_tent(rgba, w, h);
    }
    const int vw = mirror_u ? 2 * w : w, vh = mirror_v ? 2 * h : h;  // the mirrored image
    int fx = 1, fy = 1;
    while (vw / fx > kMaxSide) fx *= 2;
    while (vh / fy > kMaxSide) fy *= 2;
    int iw = vw / fx, ih = vh / fy;
    if (shrunk) {  // the GE wants power-of-two sides: the image sits in the corner, the rest clear
        int pw = 8, ph = 8;
        while (pw < w) pw *= 2;
        while (ph < h) ph *= 2;
        iw = pw, ih = ph;
    }
    const int format = entry_format(e);
    const int bpp = format == GU_PSM_8888 ? 4 : 2;
    const std::uint32_t bytes = static_cast<std::uint32_t>(iw * ih * bpp);
    if (!make_room(bytes)) {
        ++g_stats.over_budget, ++g_stats.render_over_budget;
        return false;
    }
    rgba.shrink_to_fit();
    auto* out = static_cast<std::uint8_t*>(memalign(16, bytes));
    if (!out && make_room(g_budget))
        out = static_cast<std::uint8_t*>(memalign(16, bytes));
    if (!out) {
        ++g_stats.over_budget, ++g_stats.render_over_budget;
        return false;
    }
    const auto texel = [&](int x, int y) -> std::uint32_t {
        if (shrunk && (x >= w || y >= h)) return 0;
        const int sx = x < w ? x : 2 * w - 1 - x;
        const int sy = y < h ? y : 2 * h - 1 - y;
        return rgba[static_cast<std::size_t>(sy) * w + sx];
    };
    // Swizzled layout: blocks of 16 bytes by 8 rows, row-major over the image.
    const int row_blocks = iw * bpp / 16;
    for (int y = 0; y < ih; ++y) {
        for (int x = 0; x < iw; ++x) {
            std::uint32_t c;
            if (fx == 1 && fy == 1) {
                c = texel(x, y);
            } else {
                std::uint32_t sum[4] = {};
                for (int j = 0; j < fy; ++j)
                    for (int i = 0; i < fx; ++i) {
                        const std::uint32_t s = texel(x * fx + i, y * fy + j);
                        for (int k = 0; k < 4; ++k) sum[k] += (s >> (8 * k)) & 0xFFu;
                    }
                const std::uint32_t n = static_cast<std::uint32_t>(fx * fy);
                c = 0;
                for (int k = 0; k < 4; ++k) c |= ((sum[k] + n / 2) / n) << (8 * k);
            }
            const std::uint32_t v = pack(c, format);
            const int bx = (x * bpp) >> 4, inner = (x * bpp) & 15;
            const std::size_t off = static_cast<std::size_t>((y >> 3) * row_blocks + bx) * 128 +
                                    static_cast<std::size_t>((y & 7) * 16 + inner);
            if (bpp == 4)
                std::memcpy(out + off, &v, 4);
            else {
                const std::uint16_t v16 = static_cast<std::uint16_t>(v);
                std::memcpy(out + off, &v16, 2);
            }
        }
    }
    sceKernelDcacheWritebackRange(out, bytes);
    e.memory = out;
    e.bytes = bytes;
    e.tex.pixels = out;
    e.tex.format = format;
    e.tex.width = iw;
    e.tex.height = ih;
    e.tex.u_scale = static_cast<float>(w) / static_cast<float>(fx);
    e.tex.v_scale = static_cast<float>(h) / static_cast<float>(fy);
    g_stats.bytes += bytes;
    g_stats.peak_bytes = std::max(g_stats.peak_bytes, g_stats.bytes);
    ++g_stats.decoded;
    g_stats.decode_us += clock_us() - t0;
    return true;
}

const char* format_name(int f) {
    switch (f) {
        case GU_PSM_5551: return "5551";
        case GU_PSM_5650: return "5650";
        case GU_PSM_4444: return "4444";
        default: return "8888";
    }
}

}  // namespace

void texcache_configure(bool enabled, std::uint32_t budget_bytes) {
    g_enabled = enabled;
    g_budget = budget_bytes;
    g_stats.budget = budget_bytes;
}

void texcache_begin_frame(const std::uint8_t* vram, std::size_t vram_size,
                          const std::uint32_t* palette_ram, unsigned palette_format,
                          const std::uint32_t* page_epoch, std::uint32_t epoch) {
    ++g_render;
    g_stats.render_over_budget = 0;
    g_stats.render_demand = 0;
    g_stats.render_images = 0;
    g_page_epoch = page_epoch;
    g_epoch = epoch;
    g_vram = vram;
    g_vram_size = vram_size;
    g_palette_format = palette_format & 3u;
    if (palette_ram)
        dream::render::unpack_palette(
            palette_ram, static_cast<dream::render::PaletteFormat>(g_palette_format), g_palette);
}

const GeTexture* texcache_get(std::uint32_t tcw, std::uint32_t tsp, bool mirror_u, bool mirror_v,
                              const SoftSpec& soft) {
    if (!g_enabled || !g_vram)
        return nullptr;
    ++g_stats.requests;
    std::uint32_t soft_hash = 0;  // 23 bits of the spec: a collision needs two copies of one texture
    if (soft.mode != SoftSpec::kNone) {
        soft_hash = hash_bytes(reinterpret_cast<const std::uint8_t*>(&soft.x0), 16, 2166136261u);
        soft_hash = ((soft_hash ^ soft.mode) * 16777619u) & 0x7FFFFFu;
    }
    const std::uint64_t key = (static_cast<std::uint64_t>(tcw) << 32) | (tsp & 0x3Fu) |
                              (mirror_u ? 0x40u : 0u) | (mirror_v ? 0x80u : 0u) |
                              (static_cast<std::uint64_t>(soft_hash) << 8) |
                              (soft.mode != SoftSpec::kNone ? 0x80000000u : 0u);
    auto it = g_entries.find(key);
    if (it == g_entries.end()) {
        Entry e;
        e.soft = soft;
        if (!dream::render::describe_texture(tcw, tsp, 0, e.info)) {
            e.failed = true;
            ++g_stats.failed;
        }
        it = g_entries.emplace(key, std::move(e)).first;
        g_stats.entries = static_cast<std::uint32_t>(g_entries.size());
    }
    Entry& e = it->second;
    if (e.failed)
        return nullptr;
    if (e.used != g_render) {
        // Size the GE image will have (mirrored, reduced to 512, 16 or 32 bits per texel).
        int w = static_cast<int>(e.info.width) * (mirror_u ? 2 : 1);
        int h = static_cast<int>(e.info.height) * (mirror_v ? 2 : 1);
        while (w > kMaxSide) w /= 2;
        while (h > kMaxSide) h /= 2;
        g_stats.render_demand +=
            static_cast<std::uint32_t>(w * h * (entry_format(e) == GU_PSM_8888 ? 4 : 2));
        ++g_stats.render_images;
        g_stats.peak_demand = std::max(g_stats.peak_demand, g_stats.render_demand);
    }
    e.used = g_render;
    if (e.checked != g_render) {
        const std::uint64_t t0 = clock_us();
        std::uint32_t sh = e.source_hash;
        if (!e.memory || source_dirty(e.info, e.epoch)) {
            sh = source_hash(e.info);
            ++g_stats.rehashed;
        }
        const std::uint32_t ph = palette_hash(e.info);
        e.epoch = g_epoch;
        g_stats.hash_us += clock_us() - t0;
        if (e.memory && (sh != e.source_hash || ph != e.palette_hash)) {
            release(e);
            ++g_stats.invalidated;
        }
        e.source_hash = sh;
        e.palette_hash = ph;
        e.checked = g_render;
    }
    if (e.memory) {
        ++g_stats.hits;
        return &e.tex;
    }
    if (!build(e, mirror_u, mirror_v))
        return nullptr;
    return &e.tex;
}

void texcache_release_all() {
    for (auto& [key, e] : g_entries) release(e);
    g_entries.clear();
}

TextureCacheStats texcache_stats() {
    std::uint32_t held = 0;
    for (const auto& [key, e] : g_entries)
        if (e.memory) ++held;
    TextureCacheStats s = g_stats;
    s.entries = held;
    return s;
}

void texcache_describe(void (*log)(const char*, ...)) {
    for (const auto& [key, e] : g_entries) {
        if (e.failed) {
            log("texture: tcw %08x tsp %02x rejected by the decoder",
                static_cast<unsigned>(key >> 32), static_cast<unsigned>(key & 0x3Fu));
            continue;
        }
        if (!e.memory)
            continue;
        log("texture: tcw %08x tsp %02x mirror %u%u %s -> ge %s %dx%d bytes %u used %u",
            static_cast<unsigned>(key >> 32), static_cast<unsigned>(key & 0x3Fu),
            static_cast<unsigned>((key >> 6) & 1u), static_cast<unsigned>((key >> 7) & 1u),
            e.info.describe().c_str(), format_name(e.tex.format), e.tex.width, e.tex.height,
            e.bytes, e.used);
    }
}

}  // namespace ikaruga::host
