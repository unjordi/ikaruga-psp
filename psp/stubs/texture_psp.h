// PowerVR2 textures for the GE: decoded with the submodule's device-independent decoder
// (dream/render/texture.h), repacked into the smallest GE format that keeps every bit of the
// source, swizzled, and kept in a bounded cache in main memory that the GE samples directly.
//
// Contract:
//   * texcache_begin_frame() once per render, before any texcache_get(): it takes the guest's
//     video memory and unpacks palette memory.
//   * texcache_get() returns the GE image for one (tcw, tsp size, mirroring) or nullptr when the
//     texture cannot be decoded, does not fit the budget, or textures are off. A cached image is
//     re-validated once per render by hashing the guest bytes it was decoded from (and the palette
//     entries it uses), so a guest that rewrites that video memory or palette gets a fresh decode.
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
    std::uint64_t hash_us = 0, decode_us = 0;  // time spent validating and building
};

void texcache_configure(bool enabled, std::uint32_t budget_bytes);
void texcache_begin_frame(const std::uint8_t* vram, std::size_t vram_size,
                          const std::uint32_t* palette_ram, unsigned palette_format);
const GeTexture* texcache_get(std::uint32_t tcw, std::uint32_t tsp, bool mirror_u, bool mirror_v);
TextureCacheStats texcache_stats();
// Frees every cached image (the GE must be done with them); the next render decodes again.
void texcache_release_all();
// One line per cached image: source description, GE format, size and bytes.
void texcache_describe(void (*log)(const char*, ...));

}  // namespace ikaruga::host
