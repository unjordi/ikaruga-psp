// Rounding-mode probe: sets the guest's round-toward-zero through dream::fenv and checks a float
// division whose result differs between round-to-nearest and toward-zero. Writes PASS/FAIL lines
// to ikaruga.log next to the EBOOT and exits.
#include <pspkernel.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "dream/runtime/fenv.h"

PSP_MODULE_INFO("redondeo", PSP_MODULE_USER, 0, 1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);

namespace {

std::string g_log;

void line(const char* text) {
    const SceUID fd = sceIoOpen(g_log.c_str(), PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd < 0)
        return;
    sceIoWrite(fd, text, std::strlen(text));
    sceIoWrite(fd, "\n", 1);
    sceIoClose(fd);
}

std::uint32_t bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

// 1/3: nearest rounds up to 0x3EAAAAAB, toward zero truncates to 0x3EAAAAAA.
std::uint32_t one_third() {
    volatile float a = 1.0f, b = 3.0f;
    return bits(a / b);
}

}  // namespace

int main(int argc, char** argv) {
    g_log = argc > 0 && argv[0] ? argv[0] : "ms0:/PSP/GAME/IKARUGA/EBOOT.PBP";
    g_log = g_log.substr(0, g_log.find_last_of('/') + 1) + "ikaruga.log";
    char buf[160];
    int fails = 0;
    auto check = [&](const char* what, bool ok, std::uint32_t got) {
        std::snprintf(buf, sizeof buf, "%s: %s (0x%08lx)", ok ? "PASS" : "FAIL", what,
            static_cast<unsigned long>(got));
        line(buf);
        fails += ok ? 0 : 1;
    };

    using dream::fenv::Mode;
    using dream::fenv::Rounding;
    dream::fenv::apply(Mode{Rounding::TowardZero, false});
    check("toward zero: 1/3 truncates", one_third() == 0x3EAAAAAAu, one_third());
    check("toward zero: current() reports it",
        dream::fenv::current().rounding == Rounding::TowardZero, 0);
    dream::fenv::apply(Mode{Rounding::Nearest, false});
    check("nearest: 1/3 rounds up", one_third() == 0x3EAAAAABu, one_third());
    check("nearest: current() reports it",
        dream::fenv::current().rounding == Rounding::Nearest, 0);
    dream::fenv::apply(Mode{Rounding::TowardZero, true});
    check("toward zero + flush: current() keeps both",
        dream::fenv::current().rounding == Rounding::TowardZero && dream::fenv::current().flush_denormals, 0);
    line(fails == 0 ? "result: all passed" : "result: failed");
    sceKernelExitGame();
    return 0;
}
