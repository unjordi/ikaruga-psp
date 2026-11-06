// Host services the PSP build needs from outside dream-recomp's core (table 1 of
// docs/diseno-psp.md). Video, audio and input are still stubs that do nothing.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "dream/runtime/gdrom/disc.h"
#include "dream/runtime/maple/maple.h"

namespace ikaruga::host {

// Rows 1-4: GE context, renderer, texture cache, presentation.
bool video_init();
void video_present_frame(const std::uint8_t* vram, std::uint32_t vram_size);

// Row 5: controller.
void input_poll(dream::maple::ControllerState& pad);

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

// Row 13: CPU clock.
void process_init();

}  // namespace ikaruga::host
