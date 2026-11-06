// PSP entry point: wires dream-recomp's core the way runtime/boot/boot_main.cpp does, headless,
// with every host service routed through psp/stubs/host.h. The run has no end of its own: every
// kLogEvery guest frames it appends one line to kLog until the emulator or console is stopped.
#include <pspkernel.h>

#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "dream/runtime/aica/aica.h"
#include "dream/runtime/aica/rtc.h"
#include "dream/runtime/gdrom/disc.h"
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
// The guest's call chain runs on this thread's stack, so it gets a large one, and the VFPU.
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(4096);
// Everything else that is free goes to the heap (the guest's 16 MB RAM, 8 MB VRAM, 2 MB sound RAM
// live there), minus a margin for the kernel's own allocations on our behalf.
PSP_HEAP_SIZE_KB(-1024);

namespace {

constexpr std::uint32_t kEntry = 0x8C010000u;        // games/ikaruga/ikaruga.toml: entry
constexpr std::uint32_t kLoadAddress = 0x8C010000u;  // games/ikaruga/ikaruga.toml: load_address
constexpr const char* kDefaultDir = "ms0:/PSP/GAME/IKARUGA/";
constexpr std::uint64_t kLogEvery = 60;               // guest frames between log lines
constexpr double kGuestHz = 200e6;                    // SH-4 clock the guest cycles count

std::string g_dir = kDefaultDir;

// The folder the EBOOT was started from (argv[0] is its full path, e.g.
// ms0:/PSP/GAME/OTHER/EBOOT.PBP or ef0:/...), with its trailing slash.
std::string game_dir(int argc, char** argv) {
    if (argc < 1 || !argv || !argv[0])
        return kDefaultDir;
    const std::string self = argv[0];
    const std::size_t slash = self.find_last_of('/');
    if (slash == std::string::npos || self.find(':') == std::string::npos)
        return kDefaultDir;
    return self.substr(0, slash + 1);
}

std::string path_in_dir(const char* name) { return g_dir + name; }

void log_line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void log_line(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n > static_cast<int>(sizeof buf) - 2)
        n = sizeof buf - 2;
    buf[n++] = '\n';
    ikaruga::host::file_append(path_in_dir("ikaruga.log").c_str(), buf,
        static_cast<std::uint32_t>(n));
}

// ikaruga.cfg next to the EBOOT: "key = value" lines, '#' comments. Only rtc_seed is read: the
// console clock in seconds since 1970, as the PC launcher's --rtc-seed. Default 1.
std::uint32_t rtc_seed_from_cfg() {
    std::vector<std::uint8_t> raw;
    std::uint32_t seed = 1;
    if (!ikaruga::host::file_read(path_in_dir("ikaruga.cfg").c_str(), raw))
        return seed;
    std::string text(raw.begin(), raw.end());
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        line = line.substr(0, line.find('#'));
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(0, eq);
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t\r") + 1);
        if (key == "rtc_seed")
            seed = static_cast<std::uint32_t>(std::strtoul(line.c_str() + eq + 1, nullptr, 0));
    }
    return seed;
}

// The AICA clock advances one second every 200M guest cycles, scheduled as the PC launcher does.
void start_rtc_tick(dream::System& sys, dream::aica::Rtc& rtc) {
    auto id = std::make_shared<int>(-1);
    *id = sys.sched.add("rtc", [&sys, &rtc, id](std::uint64_t, std::uint64_t) {
        rtc.tick();
        sys.sched.request(*id, 200'000'000ull);
    });
    sys.sched.request(*id, 200'000'000ull);
}

// Oracle against PC: the rolling hash of guest stores that the PC launcher's --write-hash prints,
// sampled the same way (first scanline of each new frame, from a scheduler event with the period
// of a scanline), logged every kLogEvery frames as "hash <frame> <cycles> <hash> <writes> rtc <s>".
void start_write_hash(dream::System& sys, const dream::aica::Rtc& rtc) {
    sys.memory.hash_writes = true;
    auto id = std::make_shared<int>(-1);
    auto hashed_frame = std::make_shared<std::uint64_t>(~0ull);
    const std::uint64_t period = sys.spg.line_cycles();
    *id = sys.sched.add("write-hash", [&sys, &rtc, id, hashed_frame, period](std::uint64_t now,
                                                                       std::uint64_t) {
        const std::uint64_t f = sys.spg.frames();
        if (f != *hashed_frame) {
            *hashed_frame = f;
            if (f % kLogEvery == 0)
                log_line("hash %" PRIu64 " %" PRIu64 " %016" PRIx64 " %" PRIu64 " rtc %" PRIu32, f,
                    now, sys.memory.write_hash, sys.memory.writes_hashed, rtc.seconds());
        }
        sys.sched.request(*id, period);
    });
    sys.sched.request(*id, period);
}

int quit(int rc) {
    sceKernelExitGame();
    return rc;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace ikaruga;
    g_dir = game_dir(argc, argv);
    const std::string log_path = path_in_dir("ikaruga.log");
    host::file_write(log_path.c_str(), nullptr, 0);
    host::process_init();
    host::video_init();
    host::audio_init(44100);
    log_line("ikaruga-psp: headless boot, log every %" PRIu64 " guest frames", kLogEvery);

    const std::string program_path = path_in_dir("1ST_READ.BIN");
    std::vector<std::uint8_t> program;
    if (!host::file_read(program_path.c_str(), program) || program.empty()) {
        log_line("error: cannot read %s; copy the game's 1ST_READ.BIN there", program_path.c_str());
        return quit(2);
    }
    const std::uint32_t offset = kLoadAddress & (dream::mem::DcMemory::kRamSize - 1);
    if (program.size() > dream::mem::DcMemory::kRamSize - offset) {
        log_line("error: %s is %zu bytes, larger than guest RAM", program_path.c_str(),
            program.size());
        return quit(2);
    }

    const std::string disc_path = path_in_dir("DISCO.ISO");
    std::string err;
    std::unique_ptr<dream::gdrom::Disc> disc = host::disc_open(disc_path.c_str(), err);
    if (!disc) {
        log_line("error: disc: %s; build it with scripts/preparar-disco-ikaruga.sh", err.c_str());
        return quit(2);
    }

    dream::System sys;
    dream::hle::Bios bios(sys);
    bios.attach_disc(disc.get());
    const auto region = dream::hle::Region::Japan;
    bios.set_locale(dream::hle::Language::English, region, dream::hle::broadcast_for(region));
    sys.install();
    bios.setup_boot(kEntry);
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
    // SH-4 on-chip modules the BIOS and the game touch; plain registers as on PC, the serial port
    // included (its output is not kept).
    dream::mem::RegisterFile bsc(0xFF800000u, 0x50 / 4), ubc(0xFF200000u, 0x24 / 4),
        cpg(0xFFC00000u, 0x10 / 4), sh4rtc(0xFFC80000u, 0x40 / 4), scif(0xFFE80000u, 0x28 / 4);
    dream::mem::NullRegion cache_arrays;
    sys.memory.map_p4(0xF0000000u, 0xF6000000u, &cache_arrays);
    sys.memory.map_p4(0xFF800000u, 0xFF800050u, &bsc);
    sys.memory.map_p4(0xFF200000u, 0xFF200024u, &ubc);
    sys.memory.map_p4(0xFFC00000u, 0xFFC00010u, &cpg);
    sys.memory.map_p4(0xFFC80000u, 0xFFC80040u, &sh4rtc);
    sys.memory.map_p4(0xFFE80000u, 0xFFE80028u, &scif);

    dream::maple::Bus maple(sys.sched, sys.holly, sys.memory);
    auto pad = std::make_unique<dream::maple::Controller>();
    dream::maple::Controller* pad_ptr = pad.get();
    // Row 9: the memory card image on ms0:, or a blank card when there is none.
    auto card = std::make_unique<dream::maple::MemoryCard>();
    if (!host::vmu_load(path_in_dir("VMU.BIN").c_str(), *card))
        log_line("memory card: none on ms0:, using a blank one in memory");
    maple.attach_expansion(0, 0, std::move(card));
    maple.attach(0, std::move(pad));
    sys.memory.map_mmio(dream::maple::Bus::kBase, dream::maple::Bus::kEnd, &maple);

    dream::aica::Aica aica(sys.sched, sys.holly, sys.memory);
    sys.memory.map_mmio(dream::aica::Aica::kRegBase, dream::aica::Aica::kRegEnd, &aica);
    const std::uint32_t rtc_seed = rtc_seed_from_cfg();
    dream::aica::Rtc rtc(rtc_seed + dream::aica::Rtc::kUnixEpochOffset);
    sys.memory.map_mmio(dream::aica::Rtc::kBase, dream::aica::Rtc::kEnd, &rtc);
    start_rtc_tick(sys, rtc);
    log_line("rtc: seed %" PRIu32, rtc_seed);
    start_write_hash(sys, rtc);

    const std::uint64_t t0 = host::clock_us();
    auto report = [&](const char* what) {
        const double host_s = static_cast<double>(host::clock_us() - t0) / 1e6;
        const double guest_s = static_cast<double>(sys.ctx.cycles) / kGuestHz;
        log_line("%s frames %" PRIu64 " cycles %" PRIu64 " guest_s %.3f host_s %.3f speed %.4f "
            "pc 0x%08" PRIx32,
            what, sys.spg.frames(), sys.ctx.cycles, guest_s, host_s,
            host_s > 0 ? guest_s / host_s : 0.0, sys.ctx.pc);
    };
    sys.spg.on_vblank_out = [&] {
        host::input_poll(pad_ptr->state);
        maple.vblank();
        if (sys.spg.frames() % kLogEvery == 0)
            report("tick");
    };

    log_line("start: %zu bytes of 1ST_READ.BIN at 0x%08" PRIx32 ", disc %u sectors",
        program.size(), kLoadAddress, disc->tracks().back().sectors);
    int rc = 0;
    try {
        dream::sh4::run_guest(sys.ctx, sys.memory, kEntry);
        report("stop: guest returned");
    } catch (const std::exception& e) {
        log_line("fault: %s", e.what());
        report("stop: fault");
        rc = 1;
    } catch (...) {
        report("stop: unknown exception");
        rc = 1;
    }
    return quit(rc);
}
