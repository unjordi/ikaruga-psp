// Host services the PSP build needs from outside dream-recomp's core: video (GE), textures,
// controller, clock, GD-ROM, memory card, files, power callbacks and the ikaruga.cfg reader.
// Audio is a stub that does nothing.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "dream/runtime/gdrom/disc.h"
#include "dream/render/display_list.h"
#include "dream/render/fog.h"
#include "dream/runtime/maple/maple.h"

namespace ikaruga::host {

// Video: GE context, renderer, presentation; textures in texture_psp.h.
struct VideoStats {
    std::uint32_t drawn = 0;    // strips sent to the GE
    std::uint32_t blended = 0;  // of those, translucent
    std::uint32_t textured = 0;  // strips drawn with a texture
    std::uint32_t list_bytes = 0;  // GE display list used by the render, of list_capacity
    std::uint32_t list_capacity = 0;
    std::uint32_t dropped = 0;  // strips not drawn because the display list was full
    std::uint32_t clip_inside = 0, clip_outside = 0;  // strips drawn under user clip mode 2 / 3
    std::uint32_t fogged = 0;  // strips drawn with fog
    std::uint32_t hairlines = 0;  // sub-pixel untextured quads widened to one pixel
    std::uint32_t minified = 0;   // point-sampled quads sampled bilinearly (minified)
};
bool video_init();
// Off: frames are still cleared and swapped, but no geometry is drawn.
void video_set_drawing(bool enabled);
// Off: every strip passes, whatever its depth compare.
void video_set_depth_test(bool enabled);
// Off: the polygons' user tile clip (modes 2 and 3) is ignored.
void video_set_clip(bool enabled);
// Called once per strip drawn while set (diagnostics); nullptr turns it off.
void video_set_trace(void (*trace)(const char*, ...));
// Draws one decoded render into the back buffer and shows it.
// fog: the frame's fog registers (dream::render::describe_fog), or null for none.
VideoStats video_draw_frame(const dream::render::Frame& frame, float alpha_ref,
                            const dream::render::FogSettings* fog = nullptr);
// Off: polygons are drawn without fog.
void video_set_fog(bool enabled);
void video_set_hairlines(bool enabled);
// The buffer on screen as packed RGB888, top row first.
bool video_capture(std::vector<std::uint8_t>& rgb, unsigned& width, unsigned& height);

// One "key = value" line of ikaruga.cfg.
struct CfgEntry {
    std::string key, value;
};
// The lines of the file at `path` in order ('#' starts a comment, both sides trimmed); empty
// when the file is missing.
std::vector<CfgEntry> cfg_read(const char* path);

// Controller. Takes the mapping keys of ikaruga.cfg (docs/controles-psp.md) from `cfg`; the
// other keys are ignored and, for the keys not present, the defaults stay.
void input_config(const std::vector<CfgEntry>& cfg);
// Scripted presses "name@frame,..." (PC's --press syntax), OR'd with the real buttons; returns
// how many.
std::size_t input_script(const std::string& presses);
void input_poll(dream::maple::ControllerState& pad, std::uint64_t frame);

// Audio output.
bool audio_init(unsigned rate);
void audio_push(std::int16_t left, std::int16_t right);

// Clock and pacing.
std::uint64_t clock_us();
void clock_wait_vblank();

// The GD-ROM, from an ISO image of the game area on ms0: (see psp/stubs/disc.cpp).
std::unique_ptr<dream::gdrom::Disc> disc_open(const char* path, std::string& error);

// Memory card. Loads the image at `path`; false (and a blank formatted card) without one.
bool vmu_load(const char* path, dream::maple::MemoryCard& card);

// Files on ms0:.
bool file_read(const char* path, std::vector<std::uint8_t>& out);
bool file_write(const char* path, const std::uint8_t* data, std::uint32_t size);
bool file_append(const char* path, const char* text, std::uint32_t size);

// CPU clock; HOME and power callbacks.
void process_init();
void power_init();
bool exit_requested();
// Times the console came back from suspend; descriptors opened before the last one are invalid.
unsigned resume_count();
// What the power callback does with its PSP_POWER_CB_* flags (called directly by tests).
void power_event(int flags);
// Test hook: notifies the registered exit callback as HOME -> Exit does.
void test_request_exit();
// Test hook for the disc: closes the disc's descriptor as a suspend does, keeping the stale number.
void disc_lose_handle(dream::gdrom::Disc& disc);

}  // namespace ikaruga::host
