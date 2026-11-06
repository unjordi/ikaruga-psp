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
    std::uint32_t used = 0;     // the last render that asked for it
    bool failed = false;
};

bool g_enabled = true;
std::uint32_t g_budget = 4u << 20;
std::unordered_map<std::uint64_t, Entry> g_entries;
TextureCacheStats g_stats;
std::uint32_t g_render = 0;
const std::uint8_t* g_vram = nullptr;
std::size_t g_vram_size = 0;
unsigned g_palette_format = 0;
std::uint32_t g_palette[1024];

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
    }
    return true;
}

// Decodes, mirrors, reduces, packs and swizzles `e.info` into a new image.
bool build(Entry& e, bool mirror_u, bool mirror_v) {
    const std::uint64_t t0 = clock_us();
    std::vector<std::uint32_t> rgba;
    if (!dream::render::decode_texture(e.info, g_vram, g_vram_size, g_palette, rgba)) {
        ++g_stats.failed;
        e.failed = true;
        return false;
    }
    const int w = static_cast<int>(e.info.width), h = static_cast<int>(e.info.height);
    const int vw = mirror_u ? 2 * w : w, vh = mirror_v ? 2 * h : h;  // the mirrored image
    int fx = 1, fy = 1;
    while (vw / fx > kMaxSide) fx *= 2;
    while (vh / fy > kMaxSide) fy *= 2;
    const int iw = vw / fx, ih = vh / fy;
    const int format = ge_format(e.info);
    const int bpp = format == GU_PSM_8888 ? 4 : 2;
    const std::uint32_t bytes = static_cast<std::uint32_t>(iw * ih * bpp);
    if (!make_room(bytes)) {
        ++g_stats.over_budget;
        return false;
    }
    auto* out = static_cast<std::uint8_t*>(memalign(16, bytes));
    if (!out) {
        ++g_stats.over_budget;
        return false;
    }
    const auto texel = [&](int x, int y) {
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
                          const std::uint32_t* palette_ram, unsigned palette_format) {
    ++g_render;
    g_vram = vram;
    g_vram_size = vram_size;
    g_palette_format = palette_format & 3u;
    if (palette_ram)
        dream::render::unpack_palette(
            palette_ram, static_cast<dream::render::PaletteFormat>(g_palette_format), g_palette);
}

const GeTexture* texcache_get(std::uint32_t tcw, std::uint32_t tsp, bool mirror_u, bool mirror_v) {
    if (!g_enabled || !g_vram)
        return nullptr;
    const std::uint64_t key = (static_cast<std::uint64_t>(tcw) << 32) | (tsp & 0x3Fu) |
                              (mirror_u ? 0x40u : 0u) | (mirror_v ? 0x80u : 0u);
    auto it = g_entries.find(key);
    if (it == g_entries.end()) {
        Entry e;
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
    e.used = g_render;
    if (e.checked != g_render) {
        const std::uint64_t t0 = clock_us();
        const std::uint32_t sh = source_hash(e.info), ph = palette_hash(e.info);
        g_stats.hash_us += clock_us() - t0;
        if (e.memory && (sh != e.source_hash || ph != e.palette_hash)) {
            release(e);
            ++g_stats.invalidated;
        }
        e.source_hash = sh;
        e.palette_hash = ph;
        e.checked = g_render;
    }
    if (!e.memory && !build(e, mirror_u, mirror_v))
        return nullptr;
    return &e.tex;
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
