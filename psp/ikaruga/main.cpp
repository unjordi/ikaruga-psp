// PSP entry point: wires dream-recomp's core the way runtime/boot/boot_main.cpp does, with every
// host service routed through psp/stubs/host.h.
#include <pspkernel.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

#include "dream/runtime/aica/aica.h"
#include "dream/runtime/aica/rtc.h"
#include "dream/runtime/hle/bios.h"
#include "dream/runtime/hle/flash.h"
#include "dream/runtime/holly/g2dma.h"
#include "dream/runtime/holly/sysblock.h"
#include "dream/runtime/maple/maple.h"
#include "dream/runtime/mem/dc_memory.h"
#include "dream/runtime/pvr/core.h"
#include "dream/runtime/sh4/abi.h"
#include "dream/runtime/sh4/dmac.h"
#include "dream/runtime/system.h"
#include "host.h"

PSP_MODULE_INFO("ikaruga", PSP_MODULE_USER, 0, 1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1);

namespace {

constexpr std::uint32_t kEntry = 0x8C010000u;        // games/ikaruga/ikaruga.toml: entry
constexpr std::uint32_t kLoadAddress = 0x8C010000u;  // games/ikaruga/ikaruga.toml: load_address
constexpr const char* kProgram = "ms0:/PSP/GAME/IKARUGA/1ST_READ.BIN";

}  // namespace

int main() {
    using namespace ikaruga;
    host::process_init();
    host::video_init();
    host::audio_init(44100);

    std::vector<std::uint8_t> program;
    if (!host::file_read(kProgram, program))
        std::printf("%s: not read\n", kProgram);

    dream::System sys;
    dream::hle::Bios bios(sys);
    const auto region = dream::hle::Region::Japan;
    bios.set_locale(dream::hle::Language::English, region, dream::hle::broadcast_for(region));
    sys.install();
    bios.setup_boot(kEntry);
    const std::uint32_t offset = kLoadAddress & (dream::mem::DcMemory::kRamSize - 1);
    if (program.size() <= dream::mem::DcMemory::kRamSize - offset)
        std::memcpy(sys.memory.ram() + offset, program.data(), program.size());

    dream::pvr::Core pvr(sys.sched, sys.holly, sys.spg, sys.memory);
    sys.memory.map_mmio(dream::pvr::Core::kRegBase, dream::pvr::Core::kRegEnd, &pvr);
    sys.memory.map_mmio(dream::pvr::Core::kFifoBase, dream::pvr::Core::kFifoEnd, &pvr);

    dream::sh4::Dmac dmac(sys.memory);
    dream::holly::SysBlock sysblock(dmac, sys.holly, sys.memory, pvr);
    sys.memory.map_p4(dream::sh4::Dmac::kBase, dream::sh4::Dmac::kEnd, &dmac);
    sys.memory.map_mmio(dream::holly::SysBlock::kBase, dream::holly::SysBlock::kEnd, &sysblock);
    dream::holly::G2Dma g2(sys.sched, sys.holly, sys.memory);
    dream::mem::RegisterFile g1(0x005F7400u, 0x100 / 4), pvrdma(0x005F7C00u, 0x100 / 4),
        dma_triggers(0x005F6940u, 0x20 / 4);
    sys.memory.map_mmio(0x005F6940u, 0x005F6960u, &dma_triggers);
    sys.memory.map_mmio(0x005F7400u, 0x005F7500u, &g1);
    sys.memory.map_mmio(dream::holly::G2Dma::kBase, dream::holly::G2Dma::kEnd, &g2);
    sys.memory.map_mmio(0x005F7C00u, 0x005F7D00u, &pvrdma);

    dream::maple::Bus maple(sys.sched, sys.holly, sys.memory);
    auto pad = std::make_unique<dream::maple::Controller>();
    host::input_poll(pad->state);
    maple.attach(0, std::move(pad));
    sys.memory.map_mmio(dream::maple::Bus::kBase, dream::maple::Bus::kEnd, &maple);

    dream::aica::Aica aica(sys.sched, sys.holly, sys.memory);
    sys.memory.map_mmio(dream::aica::Aica::kRegBase, dream::aica::Aica::kRegEnd, &aica);
    dream::aica::Rtc rtc(static_cast<std::uint32_t>(host::clock_us() / 1000000u) +
                         dream::aica::Rtc::kUnixEpochOffset);
    sys.memory.map_mmio(dream::aica::Rtc::kBase, dream::aica::Rtc::kEnd, &rtc);

    int rc = 0;
    try {
        dream::sh4::run_guest(sys.ctx, sys.memory, kEntry);
    } catch (const std::exception& e) {
        std::printf("fault: %s\n", e.what());
        rc = 1;
    }
    sceKernelExitGame();
    return rc;
}
