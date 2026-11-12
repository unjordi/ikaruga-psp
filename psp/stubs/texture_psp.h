// PowerVR2 textures for the GE: decoded with the submodule's device-independent decoder
// (dream/render/texture.h), repacked into the smallest GE format that keeps every bit of the
// source, swizzled, and kept in a bounded cache in main memory that the GE samples directly.
//
// Contract:
//   * texcache_begin_frame() once per render, before any texcache_get(): it takes the guest's
//     video memory and unpacks palette memory.
//   * texcache_get() returns the GE image for one (tcw, tsp size, mirroring) or nullptr when the
//     texture cannot be decoded, does not fit the budget, or textures are off. A cached image is
//     re-validated once per render: its palette entries are hashed, and its video memory is
//     hashed again only when one of its 4 KB pages was written since it was last validated (the
//     page stamps come from the runtime's VRAM write tracking). Without page stamps every image
//     is hashed every render. Either way a guest that rewrites the bytes gets a fresh decode.
//   * Pointers stay valid until the next texcache_begin_frame(): eviction only happens there or
//     while building a texture, and never frees an image the current render already handed out.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ikaruga::host {

struct GeTexture {
    const void* pixels = nullptr;  // swizzled, 16-byte aligned, in main memory
    int format = 0;                // GU_PSM_5650, 5551, 4444 or 8888
    int width = 0, height = 0;     // the image, powers of two up to 512
    // Texels per unit of guest texture coordinate (through mode samples in texels). With a
    // mirrored axis the image holds the texture and its mirror image side by side, so the image is
    // twice this along that axis.
    float u_scale = 0, v_scale = 0;
};

struct TextureCacheStats {
    std::uint32_t entries = 0;      // images held
    std::uint32_t bytes = 0;        // their memory
    std::uint32_t budget = 0;       // the configured cap
    std::uint32_t peak_bytes = 0;   // most ever held
    std::uint32_t decoded = 0;      // decodes since start (first use or content change)
    std::uint32_t invalidated = 0;  // of those, because the guest bytes or palette changed
    std::uint32_t evicted = 0;      // images dropped to stay under the budget
    std::uint32_t failed = 0;       // textures the decoder rejected
    std::uint32_t over_budget = 0;  // requests refused because the render alone exceeds the cap
    std::uint32_t requests = 0;     // texcache_get calls with the cache enabled
    std::uint32_t hits = 0;         // of those, answered with an image already held and valid
    std::uint32_t rehashed = 0;     // validations that had to hash video memory
    std::uint32_t render_over_budget = 0;  // over_budget during the current render only
    std::uint32_t render_demand = 0;  // bytes the images asked for in the current render need
    std::uint32_t render_images = 0;  // distinct images asked for in the current render
    std::uint32_t peak_demand = 0;    // largest render_demand seen
    std::uint64_t hash_us = 0, decode_us = 0;  // time spent validating and building
};

void texcache_configure(bool enabled, std::uint32_t budget_bytes);
// page_epoch[i] is the write epoch of VRAM page i (4 KB, 64-bit view offsets); `epoch` is the
// epoch writes made before this call carry. The caller then moves the runtime to epoch + 1, so
// writes during or after this render stamp a higher value. page_epoch may be null.
void texcache_begin_frame(const std::uint8_t* vram, std::size_t vram_size,
                          const std::uint32_t* palette_ram, unsigned palette_format,
                          const std::uint32_t* page_epoch = nullptr, std::uint32_t epoch = 0);
// A prefiltered copy for a quad the screen shows minified (see soften() in texture_psp.cpp).
//   kTent: the image through a 1-2-1 tent in both axes, drawn as usual.
//   kRect: an axis-aligned quad, resampled the way the 640x480 render reaches a 480x272 screen
//          (bilinear at the 640x480 pixels, box-averaged onto the screen pixels); the copy is the
//          pixels of the quad's integer screen rectangle (soft_rect) and is drawn 1:1 over it.
// Positions are in 640x480 pixels and texture coordinates in texels, both quantized to quarters so
// a still quad always asks for the same copy.
struct SoftSpec {
    enum Mode : std::uint8_t { kNone = 0, kTent = 1, kRect = 2 };
    Mode mode = kNone;
    std::int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // the quad, quarter pixels of 640x480
    std::int16_t u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // texels at x0/y0 and at x1/y1, quarter texels
};
constexpr float kSoftScaleX = 480.0f / 640.0f, kSoftScaleY = 272.0f / 480.0f;
// The integer screen rectangle (480x272 pixels) a kRect quad covers.
struct SoftRect {
    int x = 0, y = 0, w = 0, h = 0;
};
SoftRect soft_rect(const SoftSpec& s);
const GeTexture* texcache_get(std::uint32_t tcw, std::uint32_t tsp, bool mirror_u, bool mirror_v,
                              const SoftSpec* soft = nullptr);
TextureCacheStats texcache_stats();
// Frees every cached image (the GE must be done with them); the next render decodes again.
void texcache_release_all();
// One line per cached image: source description, GE format, size and bytes.
void texcache_describe(void (*log)(const char*, ...));

}  // namespace ikaruga::host
