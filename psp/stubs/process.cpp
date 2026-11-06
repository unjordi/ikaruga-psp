// Row 13: CPU clock, and the HOME (exit) and power callbacks. Both callbacks run in their own
// thread and only set flags; the main thread acts on them between guest frames.
#include <pspkernel.h>
#include <psppower.h>

#include "host.h"

namespace ikaruga::host {

namespace {

volatile int g_exit = 0;
volatile unsigned g_resumes = 0;
SceUID g_exit_cb = -1;

int exit_callback(int, int, void*) {
    g_exit = 1;
    // The main thread quits at its next guest frame; if the guest never gets there, quit anyway.
    for (int i = 0; i < 300; ++i)
        sceKernelDelayThread(10000);
    sceKernelExitGame();
    return 0;
}

int power_callback(int, int flags, void*) {
    power_event(flags);
    return 0;
}

int callback_thread(SceSize, void*) {
    g_exit_cb = sceKernelCreateCallback("ikaruga_exit", exit_callback, nullptr);
    if (g_exit_cb >= 0)
        sceKernelRegisterExitCallback(g_exit_cb);
    const SceUID power_cb = sceKernelCreateCallback("ikaruga_power", power_callback, nullptr);
    if (power_cb >= 0)
        scePowerRegisterCallback(-1, power_cb);
    sceKernelSleepThreadCB();
    return 0;
}

}  // namespace

// Run the Allegrex at its maximum (CPU 333 MHz, bus 166 MHz).
void process_init() { scePowerSetClockFrequency(333, 333, 166); }

void power_init() {
    const SceUID th = sceKernelCreateThread("ikaruga_callbacks", callback_thread, 0x11, 0x1000, 0,
        nullptr);
    if (th >= 0)
        sceKernelStartThread(th, 0, nullptr);
}

bool exit_requested() { return g_exit != 0; }

unsigned resume_count() { return g_resumes; }

void power_event(int flags) {
    // Descriptors on ms0: are invalid after a resume; readers compare resume_count() and reopen.
    if (flags & PSP_POWER_CB_RESUME_COMPLETE)
        g_resumes = g_resumes + 1;
}

void test_request_exit() {
    if (g_exit_cb >= 0)
        sceKernelNotifyCallback(g_exit_cb, 0);
}

}  // namespace ikaruga::host
