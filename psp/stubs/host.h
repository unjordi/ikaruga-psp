// Host services the PSP build needs from outside dream-recomp's core (table 1 of
// docs/diseno-psp.md). Video, audio and input are still stubs that do nothing.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "dream/runtime/gdrom/disc.h"
#include "dream/render/display_list.h"
#include "dream/runtime/maple/maple.h"

namespace ikaruga::host {

// Rows 1-4: GE context, renderer, presentation; textures in texture_psp.h.
struct VideoStats {
    std::uint32_t drawn = 0;    // strips sent to the GE
    std::uint32_t blended = 0;  // of those, translucent
    std::uint32_t textured = 0;  // strips drawn with a texture
    std::uint32_t list_bytes = 0;  // GE display list used by the render, of list_capacity
    std::uint32_t list_capacity = 0;
    std::uint32_t dropped = 0;  // strips not drawn because the display list was full
};
bool video_init();
// Off: frames are still cleared and swapped, but no geometry is drawn.
void video_set_drawing(bool enabled);
// Off: every strip passes, whatever its depth compare.
void video_set_depth_test(bool enabled);
// Called once per strip drawn while set (diagnostics); nullptr turns it off.
void video_set_trace(void (*trace)(const char*, ...));
// Draws one decoded render into the back buffer and shows it.
VideoStats video_draw_frame(const dream::render::Frame& frame, float alpha_ref);
// The buffer on screen as packed RGB888, top row first.
bool video_capture(std::vector<std::uint8_t>& rgb, unsigned& width, unsigned& height);

// Row 5: controller. Scripted presses "name@frame,..." (PC's --press syntax); returns how many.
std::size_t input_script(const std::string& presses);
void input_poll(dream::maple::ControllerState& pad, std::uint64_t frame);

// Row 6: audio output.
bool audio_init(unsigned rate);
void audio_push(std::int16_t left, std::int16_t right);

// Row 7: clock and pacing.
std::uint64_t clock_us();
void clock_wait_vblank();

// Row 8: the GD-ROM, from an ISO image of the game area on ms0: (see psp/stubs/disc.cpp).
std::unique_ptr<dream::gdrom::Disc> disc_open(const char* path, std::string& error);

// Row 9: memory card. Loads the image at `path`; false (and a blank formatted card) without one.
bool vmu_load(const char* path, dream::maple::MemoryCard& card);

// Rows 10-11: files on ms0:.
bool file_read(const char* path, std::vector<std::uint8_t>& out);
bool file_write(const char* path, const std::uint8_t* data, std::uint32_t size);
bool file_append(const char* path, const char* text, std::uint32_t size);

// Row 13: CPU clock; HOME and power callbacks.
void process_init();
void power_init();
bool exit_requested();
// Times the console came back from suspend; descriptors opened before the last one are invalid.
unsigned resume_count();
// What the power callback does with its PSP_POWER_CB_* flags (called directly by tests).
void power_event(int flags);
// Test hook: notifies the registered exit callback as HOME -> Exit does.
void test_request_exit();
// Test hook for row 8: closes the disc's descriptor as a suspend does, keeping the stale number.
void disc_lose_handle(dream::gdrom::Disc& disc);

}  // namespace ikaruga::host
