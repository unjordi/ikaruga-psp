#include <pspdisplay.h>
#include <psprtc.h>
#include <pspthreadman.h>

#include "host.h"

namespace ikaruga::host {

std::uint64_t clock_us() { return static_cast<std::uint64_t>(sceKernelGetSystemTimeWide()); }

void clock_wait_vblank() { sceDisplayWaitVblankStart(); }

}  // namespace ikaruga::host
