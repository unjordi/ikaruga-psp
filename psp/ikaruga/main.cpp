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
#include <ctime>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "dream/render/background.h"
#include "dream/render/display_list.h"
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
constexpr std::uint32_t kLinkAddress = 0x0C010000u;  // games/ikaruga/ikaruga.toml: link_address
// games/ikaruga/ikaruga.toml [idle]: wait-loop head -> kind (docs/idle-skip.md). A file named
// kNoIdleSkip in kDir leaves the table empty, as --no-idle-skip does on PC.
struct IdleEntry {
    std::uint32_t addr;
    const char* kind;
};
constexpr IdleEntry kIdle[] = {{0x0C0370BAu, "katana_wait_vblank"}};
constexpr const char* kNoIdleSkip = "NO_IDLE_SKIP";
constexpr const char* kDir = "ms0:/PSP/GAME/IKARUGA/";
constexpr std::uint64_t kLogEvery = 60;               // guest frames between log lines
constexpr double kGuestHz = 200e6;                    // SH-4 clock the guest cycles count

std::string path_in_dir(const char* name) { return std::string(kDir) + name; }

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

// ms0:/PSP/GAME/IKARUGA/ikaruga.cfg, one key=value per line:
//   capturar_en=N   after the first render at or past guest frame N is drawn, write the screen to
//                   captura-N.raw (RGB888, top row first) and quit
//   max_cuadros=N   quit at guest frame N
//   pulsar=...      scripted presses, PC's --press syntax (start@700,a@1400)
//   dibujar=0       clear and swap every frame but draw no geometry
//   trazar=1        log every strip of the captured render
//   profundidad=0   draw with the depth test off
//   volcar_ta=1     also write the captured render's parameter stream to ta-N.bin
//   rtc_semilla=N   console clock fixed at N seconds since 1970 (PC's --rtc-seed)
struct Config {
    std::uint64_t capture_at = 0, max_frames = 0;
    std::string presses;
    bool draw = true;
    bool trace = false;
    bool depth = true;
    std::uint64_t rtc_seed = 0;
    bool dump_ta = false;
};

Config read_config() {
    Config cfg;
    std::vector<std::uint8_t> raw;
    if (!ikaruga::host::file_read(path_in_dir("ikaruga.cfg").c_str(), raw))
        return cfg;
    std::string text(raw.begin(), raw.end());
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        const std::size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "capturar_en")
            cfg.capture_at = std::strtoull(value.c_str(), nullptr, 0);
        else if (key == "max_cuadros")
            cfg.max_frames = std::strtoull(value.c_str(), nullptr, 0);
        else if (key == "pulsar")
            cfg.presses = value;
        else if (key == "volcar_ta")
            cfg.dump_ta = value != "0";
        else if (key == "rtc_semilla")
            cfg.rtc_seed = std::strtoull(value.c_str(), nullptr, 0);
        else if (key == "profundidad")
            cfg.depth = value != "0";
        else if (key == "trazar")
            cfg.trace = value != "0";
        else if (key == "dibujar")
            cfg.draw = value != "0";
    }
    return cfg;
}

// Ends run_guest from inside a hook.
struct StopRun {
    const char* why;
};

int quit(int rc) {
    sceKernelExitGame();
    return rc;
}

}  // namespace

int main() {
    using namespace ikaruga;
    const std::string log_path = path_in_dir("ikaruga.log");
    host::file_write(log_path.c_str(), nullptr, 0);
    host::process_init();
    host::video_init();
    host::audio_init(44100);
    log_line("ikaruga-psp: log every %" PRIu64 " guest frames", kLogEvery);
    const Config cfg = read_config();
    host::video_set_drawing(cfg.draw);
    host::video_set_depth_test(cfg.depth);
    const std::size_t presses = host::input_script(cfg.presses);
    log_line("config: capturar_en %" PRIu64 " max_cuadros %" PRIu64 " presses %zu dibujar %d "
        "rtc_semilla %" PRIu64, cfg.capture_at, cfg.max_frames, presses, cfg.draw ? 1 : 0,
        cfg.rtc_seed);

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
    std::vector<std::uint8_t> flag;
    const bool idle_skip = !host::file_read(path_in_dir(kNoIdleSkip).c_str(), flag);
    if (idle_skip) {
        for (const IdleEntry& e : kIdle) {
            dream::System::IdleKind kind;
            if (!dream::System::idle_kind_from_name(e.kind, kind)) {
                log_line("error: [idle] 0x%08" PRIx32 ": unknown kind '%s'", e.addr, e.kind);
                return quit(2);
            }
            const std::uint32_t head =
                (e.addr & 0x1FFFFFFFu) - (kLoadAddress & 0x1FFFFFFFu) + kLinkAddress;
            sys.idle_loops[head] = kind;
        }
    }
    log_line("idle-skip: %s (%zu loops)", idle_skip ? "on" : "off", sys.idle_loops.size());
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
    // The console clock: fixed by rtc_semilla (PC's --rtc-seed) so a run repeats, and ticked once
    // per guest second as on PC.
    dream::aica::Rtc rtc(
        (cfg.rtc_seed ? static_cast<std::uint32_t>(cfg.rtc_seed)
                      : static_cast<std::uint32_t>(std::time(nullptr))) +
        dream::aica::Rtc::kUnixEpochOffset);
    sys.memory.map_mmio(dream::aica::Rtc::kBase, dream::aica::Rtc::kEnd, &rtc);
    int rtc_tick = -1;
    rtc_tick = sys.sched.add("rtc", [&](std::uint64_t, std::uint64_t) {
        rtc.tick();
        sys.sched.request(rtc_tick, 200'000'000ull);
    });
    sys.sched.request(rtc_tick, 200'000'000ull);

    const std::uint64_t t0 = host::clock_us();
    auto report = [&](const char* what) {
        const double host_s = static_cast<double>(host::clock_us() - t0) / 1e6;
        const double guest_s = static_cast<double>(sys.ctx.cycles) / kGuestHz;
        // pc stays the last field: scripts/test-psp-arranque.sh reads it with awk $NF.
        log_line("%s frames %" PRIu64 " cycles %" PRIu64 " guest_s %.3f host_s %.3f speed %.4f "
            "idle_skips %" PRIu64 " idle_cycles %" PRIu64 " pc 0x%08" PRIx32,
            what, sys.spg.frames(), sys.ctx.cycles, guest_s, host_s,
            host_s > 0 ? guest_s / host_s : 0.0, sys.idle_skips, sys.idle_skipped_cycles,
            sys.ctx.pc);
    };
    // Rows 1-4: every render the guest starts is decoded and drawn. The polygon count is the
    // decoded stream's, before the background plane is added, so it matches the PC launcher's.
    dream::render::DisplayList decoder;
    std::uint64_t renders = 0, draw_us = 0;
    pvr.on_render = [&](const std::vector<std::uint32_t>& stream) {
        const std::uint64_t r0 = host::clock_us();
        decoder.reset();
        decoder.feed_stream(stream.data(), stream.size());
        dream::render::Frame& frame = decoder.frame();
        const std::size_t polygons = frame.polygon_count();
        const bool background = dream::render::add_background(frame, pvr.reg_block(),
            sys.memory.vram(), dream::mem::DcMemory::kVramSize);
        const float alpha_ref = static_cast<float>(pvr.reg(0x11C) & 0xFFu) / 255.0f;
        const bool last = cfg.capture_at && sys.spg.frames() >= cfg.capture_at;
        if (last && cfg.trace)
            host::video_set_trace(&log_line);
        const host::VideoStats st = host::video_draw_frame(frame, alpha_ref);
        host::video_set_trace(nullptr);
        draw_us += host::clock_us() - r0;
        ++renders;
        const std::uint64_t f = sys.spg.frames();
        if (cfg.capture_at && f >= cfg.capture_at) {
            log_line("render: frame %" PRIu64 " render %" PRIu64 " words %zu polygons %zu "
                "opaque %zu punch %zu translucent %zu vertices %zu background %d drawn %" PRIu32
                " blended %" PRIu32 " ge_list_bytes %" PRIu32 " of %" PRIu32 " dropped %" PRIu32,
                f, renders, stream.size(), polygons,
                frame.lists[static_cast<unsigned>(dream::render::ListType::Opaque)].size(),
                frame.lists[static_cast<unsigned>(dream::render::ListType::PunchThrough)].size(),
                frame.lists[static_cast<unsigned>(dream::render::ListType::Translucent)].size(),
                frame.vertices.size(), background ? 1 : 0, st.drawn, st.blended,
                st.list_bytes, st.list_capacity, st.dropped);
            if (cfg.dump_ta) {
                char ta[64];
                std::snprintf(ta, sizeof ta, "ta-%" PRIu64 ".bin", cfg.capture_at);
                host::file_write(path_in_dir(ta).c_str(),
                    reinterpret_cast<const std::uint8_t*>(stream.data()),
                    static_cast<std::uint32_t>(stream.size() * 4));
            }
            std::vector<std::uint8_t> rgb;
            unsigned w = 0, h = 0;
            char name[64];
            std::snprintf(name, sizeof name, "captura-%" PRIu64 ".raw", cfg.capture_at);
            if (host::video_capture(rgb, w, h) &&
                host::file_write(path_in_dir(name).c_str(), rgb.data(),
                    static_cast<std::uint32_t>(rgb.size())))
                log_line("capture: %s width %u height %u format rgb24", name, w, h);
            else
                log_line("capture: failed to write %s", name);
            throw StopRun{"capture written"};
        }
    };

    sys.spg.on_vblank_out = [&] {
        const std::uint64_t f = sys.spg.frames();
        host::input_poll(pad_ptr->state, f);
        maple.vblank();
        if (f % kLogEvery == 0) {
            report("tick");
            log_line("video: renders %" PRIu64 " draw_ms_per_render %.2f", renders,
                renders ? static_cast<double>(draw_us) / 1000.0 / renders : 0.0);
        }
        if (cfg.max_frames && f >= cfg.max_frames)
            throw StopRun{"max_cuadros reached"};
    };

    log_line("start: %zu bytes of 1ST_READ.BIN at 0x%08" PRIx32 ", disc %u sectors",
        program.size(), kLoadAddress, disc->tracks().back().sectors);
    int rc = 0;
    try {
        dream::sh4::run_guest(sys.ctx, sys.memory, kEntry);
        report("stop: guest returned");
    } catch (const StopRun& s) {
        report(s.why);
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
