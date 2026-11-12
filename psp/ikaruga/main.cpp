// PSP entry point: wires dream-recomp's core the way runtime/boot/boot_main.cpp does, with every
// host service routed through psp/stubs/host.h. The game runs until HOME -> Exit or a stop key of
// ikaruga.cfg; every kLogEvery guest frames it appends a status line to ikaruga.log.
#include <malloc.h>
#include <pspkernel.h>
#include <psppower.h>

#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "dream/runtime/perf.h"
#include "dream/runtime/fenv.h"
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
#include "dream/runtime/state.h"
#include "dream/runtime/system.h"
#include "host.h"
#include "texture_psp.h"

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
constexpr std::uint32_t kLinkAddress = 0x0C010000u;  // games/ikaruga/ikaruga.toml: link_address
// games/ikaruga/ikaruga.toml [idle]: wait-loop head -> kind (docs/idle-skip.md). A file named
// kNoIdleSkip in the game folder leaves the table empty, as --no-idle-skip does on PC.
struct IdleEntry {
    std::uint32_t addr;
    const char* kind;
};
constexpr IdleEntry kIdle[] = {{0x0C0370BAu, "katana_wait_vblank"}};
constexpr const char* kNoIdleSkip = "NO_IDLE_SKIP";
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

// ikaruga.cfg next to the EBOOT: "clave = valor" lines, '#' starts a comment. The controller keys
// (control, umbral_stick, tate, boton_*) are read by input.cpp (docs/controles-psp.md).
//   rtc_semilla=N     console clock at N seconds since 1970, as PC's --rtc-seed (default 1)
//   hash_cada=N       frames between "hash" lines (default 60; 1 finds the first frame that
//                     differs from PC)
//   log_desde=I       with log_cantidad=N: guest stores [I, I+N) go to writes.log
//   prueba_salida_en=N     at guest frame N notify the exit callback (as HOME -> Exit)
//   prueba_suspension_en=N at guest frame N run a suspend and resume
//   capturar_en=N     after the first render at or past guest frame N is drawn, write the screen
//                     to captura-N.raw (RGB888, top row first) and quit
//   max_cuadros=N     quit at guest frame N
//   pulsar=...        scripted presses, PC's --press syntax (start@700,a@1400)
//   cargar_estado=F   start from the saved state F instead of the boot (docs/estado-guardado.md)
//   guardar_estado_en=N  write estado-N.bin at the first wait for vblank at or past frame N
//   dibujar=0         clear and swap every frame but draw no geometry
//   dibujar_desde=N   do not call the video at all before guest frame N (the guest runs the same;
//                     the video only reads guest state): faster runs that capture frame >= N
//   trazar=1          log every strip of the captured render
//   profundidad=0     draw with the depth test off
//   volcar_ta=1       also write the captured render's parameter stream to ta-N.bin
//   texturas=0        draw textured polygons in their vertex colour only
//   texturas_kb=N     cap of the texture cache in KB (default 2560)
//   recorte=0         ignore the polygons' user tile clip (modes 2 and 3)
//   niebla=0          draw without fog
//   memoria=1         memory counter (docs/memoria-psp.md): every memoria_cada guest frames and at
//                     the capture, a "mem:" line with the free memory and the heap peaks
//   memoria_cada=N    frames between "mem:" lines (default 60)
// Keys that used to be spelled in English are ignored with a warning in the log (kRenamed).
struct Config {
    std::uint32_t rtc_seed = 1;
    std::uint32_t hash_every = kLogEvery;
    std::uint64_t log_from = 0, log_count = 0;
    std::uint32_t test_exit_at = 0, test_suspend_at = 0;
    std::uint64_t capture_at = 0, max_frames = 0;
    std::uint64_t draw_from = 0;
    std::string presses;
    // Saved state (docs/estado-guardado.md): load_state = file to start from instead of the boot;
    // save_state_at = N writes estado-N.bin at the first wait for vblank of frame >= N.
    std::string load_state;
    std::uint64_t save_state_at = 0;
    bool draw = true;
    bool trace = false;
    bool depth = true;
    bool dump_ta = false;
    bool textures = true;
    std::uint32_t texture_kb = 2560;
    bool clip = true;
    bool fog = true;
    bool memory = false;
    std::uint32_t memory_every = kLogEvery;
};

struct RenamedKey {
    const char* old_name;
    const char* name;
};
constexpr RenamedKey kRenamed[] = {
    {"rtc_seed", "rtc_semilla"},
    {"hash_every", "hash_cada"},
    {"log_from", "log_desde"},
    {"log_count", "log_cantidad"},
    {"test_exit_at", "prueba_salida_en"},
    {"test_suspend_at", "prueba_suspension_en"},
};

Config read_config(const std::vector<ikaruga::host::CfgEntry>& entries) {
    Config cfg;
    for (const auto& [key, value] : entries) {
        const auto number = [&value] { return std::strtoull(value.c_str(), nullptr, 0); };
        for (const RenamedKey& r : kRenamed)
            if (key == r.old_name)
                log_line("warning: ikaruga.cfg key '%s' is ignored; its name is now '%s'", r.old_name,
                    r.name);
        if (key == "rtc_semilla")
            cfg.rtc_seed = static_cast<std::uint32_t>(number());
        else if (key == "hash_cada")
            cfg.hash_every = static_cast<std::uint32_t>(number());
        else if (key == "log_desde")
            cfg.log_from = number();
        else if (key == "log_cantidad")
            cfg.log_count = number();
        else if (key == "prueba_salida_en")
            cfg.test_exit_at = static_cast<std::uint32_t>(number());
        else if (key == "prueba_suspension_en")
            cfg.test_suspend_at = static_cast<std::uint32_t>(number());
        else if (key == "capturar_en")
            cfg.capture_at = number();
        else if (key == "max_cuadros")
            cfg.max_frames = number();
        else if (key == "pulsar")
            cfg.presses = value;
        else if (key == "cargar_estado")
            cfg.load_state = value;
        else if (key == "guardar_estado_en")
            cfg.save_state_at = number();
        else if (key == "volcar_ta")
            cfg.dump_ta = value != "0";
        else if (key == "profundidad")
            cfg.depth = value != "0";
        else if (key == "trazar")
            cfg.trace = value != "0";
        else if (key == "dibujar_desde")
            cfg.draw_from = number();
        else if (key == "dibujar")
            cfg.draw = value != "0";
        else if (key == "texturas")
            cfg.textures = value != "0";
        else if (key == "texturas_kb")
            cfg.texture_kb = static_cast<std::uint32_t>(number());
        else if (key == "recorte")
            cfg.clip = value != "0";
        else if (key == "niebla")
            cfg.fog = value != "0";
        else if (key == "memoria")
            cfg.memory = value != "0";
        else if (key == "memoria_cada")
            cfg.memory_every = static_cast<std::uint32_t>(number());
    }
    if (cfg.hash_every == 0)
        cfg.hash_every = kLogEvery;
    if (cfg.memory_every == 0)
        cfg.memory_every = kLogEvery;
    return cfg;
}

// Free RAM: what the heap has not handed out (inside the arena plus the largest block still
// obtainable, probed by allocating), and the kernel's free user memory outside the heap.
void log_memory(const char* tag) {
    const struct mallinfo mi = mallinfo();
    std::size_t lo = 0, hi = 64u << 20;
    while (hi - lo > 16 * 1024) {
        const std::size_t mid = lo + (hi - lo) / 2;
        // Through a volatile so the compiler cannot drop the malloc/free pair.
        void* volatile p = std::malloc(mid);
        if (p) {
            std::free(p);
            lo = mid;
        } else {
            hi = mid;
        }
    }
    log_line("%s: heap arena %d in_use %d free_in_arena %d largest_block_kb %zu "
        "kernel_free_kb %u", tag, mi.arena, mi.uordblks, mi.fordblks, lo / 1024,
        static_cast<unsigned>(sceKernelTotalFreeMemSize() / 1024));
}

// Memory counter (memoria=1): highs and lows seen at every sample. newlib's heap only asks the
// system for more through sbrk, so `arena` is what it has requested and `in_use` what the program
// holds inside it. The arena never shrinks, so its last value is its peak.
struct MemoryPeaks {
    std::size_t arena = 0, in_use = 0;
    std::uint32_t min_total_free = ~0u, min_max_free = ~0u;
} g_peaks;

void sample_memory() {
    const struct mallinfo mi = mallinfo();
    g_peaks.arena = std::max<std::size_t>(g_peaks.arena, mi.arena);
    g_peaks.in_use = std::max<std::size_t>(g_peaks.in_use, mi.uordblks);
    g_peaks.min_total_free = std::min<std::uint32_t>(g_peaks.min_total_free,
        sceKernelTotalFreeMemSize());
    g_peaks.min_max_free = std::min<std::uint32_t>(g_peaks.min_max_free,
        sceKernelMaxFreeMemSize());
}

void log_memory_counter(const char* tag, std::uint64_t frame) {
    sample_memory();
    const struct mallinfo mi = mallinfo();
    log_line("mem: %s frame %" PRIu64 " sys_total_free_kb %u sys_max_free_kb %u arena %zu in_use %zu "
        "free_in_arena %zu | peak arena %zu in_use %zu min_sys_total_free_kb %u "
        "min_sys_max_free_kb %u", tag, frame,
        static_cast<unsigned>(sceKernelTotalFreeMemSize() / 1024),
        static_cast<unsigned>(sceKernelMaxFreeMemSize() / 1024), std::size_t(mi.arena),
        std::size_t(mi.uordblks), std::size_t(mi.fordblks), g_peaks.arena, g_peaks.in_use, g_peaks.min_total_free / 1024,
        g_peaks.min_max_free / 1024);
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
// of a scanline), logged every `every` frames as "hash <frame> <cycles> <hash> <writes> rtc <s>".
void start_write_hash(dream::System& sys, const dream::aica::Rtc& rtc, std::uint64_t every) {
    sys.memory.hash_writes = true;
    auto id = std::make_shared<int>(-1);
    auto hashed_frame = std::make_shared<std::uint64_t>(~0ull);
    const std::uint64_t period = sys.spg.line_cycles();
    *id = sys.sched.add("write-hash",
        [&sys, &rtc, id, hashed_frame, period, every](std::uint64_t now, std::uint64_t) {
        const std::uint64_t f = sys.spg.frames();
        if (f != *hashed_frame) {
            *hashed_frame = f;
            if (f % every == 0)
                log_line("hash %" PRIu64 " %" PRIu64 " %016" PRIx64 " %" PRIu64 " rtc %" PRIu32, f,
                    now, sys.memory.write_hash, sys.memory.writes_hashed, rtc.seconds());
        }
        sys.sched.request(*id, period);
    });
    sys.sched.request(*id, period);
}

// Debug aid for a divergence from PC: guest stores with index in [log_from, log_from+log_count)
// go to writes.log in the PC launcher's --write-log format, so the two can be diffed line by line.
void start_write_log(dream::System& sys, std::uint64_t from, std::uint64_t count) {
    if (count == 0)
        return;
    auto buf = std::make_shared<std::string>();
    const std::string path = path_in_dir("writes.log");
    ikaruga::host::file_write(path.c_str(), nullptr, 0);
    sys.memory.log_from = from;
    sys.memory.log_to = from + count;
    sys.memory.on_hashed_write = [&sys, buf, path, last = from + count - 1](std::uint64_t index,
        std::uint32_t addr, std::uint64_t value, unsigned size) {
        char line[96];
        const int n = std::snprintf(line, sizeof line,
            "%llu %08lx %u %016llx pc %08lx pr %08lx\n", static_cast<unsigned long long>(index),
            static_cast<unsigned long>(addr), size, static_cast<unsigned long long>(value),
            static_cast<unsigned long>(sys.ctx.pc), static_cast<unsigned long>(sys.ctx.pr));
        buf->append(line, n);
        if (buf->size() > 60000 || index == last) {
            ikaruga::host::file_append(path.c_str(), buf->data(),
                static_cast<std::uint32_t>(buf->size()));
            buf->clear();
        }
    };
}

// One boot line on how this FPU treats denormals with FCSR.FS set, as the runtime sets it for
// the guest's FPSCR.DN (x86 with FTZ/DAZ gives 0 for all of them).
void log_fpu_probe() {
    dream::fenv::apply(dream::fenv::Mode{dream::fenv::Rounding::TowardZero, true});
    volatile float denormal = 1e-39f, big = 1e10f, tiny = 1e-20f;
    const float daz = denormal * big;   // denormal input
    const float ftz = tiny * tiny;      // denormal result
    volatile double dd = static_cast<double>(denormal) * 1e10;  // soft-double path
    const float dz = static_cast<float>(static_cast<double>(tiny) * tiny);
    const bool fs = dream::fenv::current().flush_denormals;
    dream::fenv::reset_host();
    std::uint32_t a, b, d;
    std::memcpy(&a, &daz, 4);
    std::memcpy(&b, &ftz, 4);
    std::memcpy(&d, &dz, 4);
    log_line("fpu: fs %d denormal*1e10 %08lx tiny*tiny %08lx soft-double in %g out %08lx",
        fs ? 1 : 0, static_cast<unsigned long>(a), static_cast<unsigned long>(b),
        static_cast<double>(dd), static_cast<unsigned long>(d));
}

struct ExitRequested {};

// Debug keys of ikaruga.cfg: prueba_salida_en = frame notifies the exit callback (as HOME -> Exit);
// prueba_suspension_en = frame runs a suspend and resume: the power events, with the disc's descriptor
// closed in between as the kernel does.
void test_hooks(std::uint64_t frame, std::uint32_t exit_at, std::uint32_t suspend_at,
    dream::gdrom::Disc& disc) {
    if (exit_at && frame == exit_at) {
        log_line("test: exit callback at frame %" PRIu64, frame);
        ikaruga::host::test_request_exit();
    }
    if (suspend_at && frame == suspend_at) {
        log_line("test: suspend and resume at frame %" PRIu64, frame);
        ikaruga::host::power_event(PSP_POWER_CB_SUSPENDING);
        ikaruga::host::disc_lose_handle(disc);
        ikaruga::host::power_event(PSP_POWER_CB_RESUME_COMPLETE);
    }
}

// Between guest frames: leave run_guest when HOME -> Exit was chosen.
void check_exit() {
    if (ikaruga::host::exit_requested())
        throw ExitRequested{};
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

int main(int argc, char** argv) {
    using namespace ikaruga;
    g_dir = game_dir(argc, argv);
    const std::string log_path = path_in_dir("ikaruga.log");
    host::file_write(log_path.c_str(), nullptr, 0);
    host::process_init();
    host::power_init();
    host::video_init();
    host::audio_init(44100);
    log_line("ikaruga-psp: log every %" PRIu64 " guest frames", kLogEvery);
    log_fpu_probe();
    const std::vector<host::CfgEntry> cfg_entries = host::cfg_read(path_in_dir("ikaruga.cfg").c_str());
    const Config cfg = read_config(cfg_entries);
    if (cfg.memory) {
        log_memory("heap-at-start");  // arena + largest_block = the whole heap, before the guest
        log_memory_counter("start", 0);
    }
    host::video_set_drawing(cfg.draw);
    host::video_set_depth_test(cfg.depth);
    host::video_set_clip(cfg.clip);
    host::video_set_fog(cfg.fog);
    host::texcache_configure(cfg.textures, cfg.texture_kb * 1024u);
    host::input_config(cfg_entries);
    const std::size_t presses = host::input_script(cfg.presses);
    log_line("config: capturar_en %" PRIu64 " max_cuadros %" PRIu64 " presses %zu dibujar %d "
        "dibujar_desde %" PRIu64 " rtc_semilla %" PRIu32 " texturas %d texturas_kb %" PRIu32,
        cfg.capture_at, cfg.max_frames, presses, cfg.draw ? 1 : 0, cfg.draw_from, cfg.rtc_seed,
        cfg.textures ? 1 : 0, cfg.texture_kb);

    const std::string program_path = path_in_dir("1ST_READ.BIN");
    std::vector<std::uint8_t> program;
    if (!host::file_read(program_path.c_str(), program) || program.empty()) {
        log_line("error: cannot read %s; copy the game's 1ST_READ.BIN there (docs/primer-uso.md)", program_path.c_str());
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
        log_line("error: disc: %s; DISCO.ISO is the dump of the game area made with tools/volcar-area-hd "
            "(docs/primer-uso.md)", err.c_str());
        return quit(2);
    }

    dream::System sys;
    dream::hle::Bios bios(sys);
    bios.attach_disc(disc.get());
    const auto region = dream::hle::Region::Japan;
    bios.set_locale(dream::hle::Language::English, region, dream::hle::broadcast_for(region));
    sys.install();
    if (cfg.memory)
        log_memory_counter("system", 0);
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

    if (cfg.memory)
        log_memory_counter("program", 0);
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
    // The memory card image on ms0:, or a blank card when there is none.
    auto card = std::make_unique<dream::maple::MemoryCard>();
    if (host::vmu_load(path_in_dir("VMU.BIN").c_str(), *card))
        log_line("memory card: read VMU.BIN");
    else
        log_line("memory card: no VMU.BIN, formatted a blank one (saved on the first write)");
    maple.attach_expansion(0, 0, std::move(card));
    maple.attach(0, std::move(pad));
    sys.memory.map_mmio(dream::maple::Bus::kBase, dream::maple::Bus::kEnd, &maple);

    dream::aica::Aica aica(sys.sched, sys.holly, sys.memory);
    sys.memory.map_mmio(dream::aica::Aica::kRegBase, dream::aica::Aica::kRegEnd, &aica);
    dream::aica::Rtc rtc(cfg.rtc_seed + dream::aica::Rtc::kUnixEpochOffset);
    sys.memory.map_mmio(dream::aica::Rtc::kBase, dream::aica::Rtc::kEnd, &rtc);
    start_rtc_tick(sys, rtc);
    log_line("rtc: seed %" PRIu32, cfg.rtc_seed);
    start_write_hash(sys, rtc, cfg.hash_every);
    start_write_log(sys, cfg.log_from, cfg.log_count);

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
    // Every render the guest starts is decoded and drawn. The polygon count is the
    // decoded stream's, before the background plane is added, so it matches the PC launcher's.
    dream::render::DisplayList decoder;
    std::uint64_t renders = 0, draw_us = 0;
    // Renders the guest pointed at a texture (FB_W_SOF1 bit 24), the last one's target address.
    std::uint64_t texture_renders = 0;
    std::uint32_t texture_target = 0;
    // Per-frame averages since the previous perf line: guest FPU vector ops, NonLocalReturn
    // throws (also per host second), scheduler callbacks, texture cache traffic and draw time.
    struct PerfMark {
        std::uint64_t frame = 0, host_us = 0, renders = 0, draw_us = 0;
        dream::perf::Counters c;
        host::TextureCacheStats t;
    } mark;
    mark.host_us = host::clock_us();
    auto log_perf = [&](std::uint64_t f) {
        const dream::perf::Counters& c = dream::perf::counters;
        const host::TextureCacheStats t = host::texcache_stats();
        const std::uint64_t now = host::clock_us();
        const double n = f > mark.frame ? static_cast<double>(f - mark.frame) : 1.0;
        const double secs = now > mark.host_us ? (now - mark.host_us) / 1e6 : 1.0;
        const double rn = renders > mark.renders ? static_cast<double>(renders - mark.renders) : 1.0;
        log_line("perf: frames %" PRIu64 "-%" PRIu64 " fipr/f %.0f ftrv/f %.0f fmac/f %.0f "
            "throw/f %.1f throw/s %.0f sched/f %.0f tex_req/f %.0f tex_hit/f %.0f "
            "tex_build/f %.2f tex_rej/f %.1f tex_rehash/f %.1f tex_ms/f %.2f draw_ms/render %.2f",
            mark.frame, f, (c.fipr - mark.c.fipr) / n, (c.ftrv - mark.c.ftrv) / n,
            (c.fmac - mark.c.fmac) / n, (c.throws - mark.c.throws) / n,
            (c.throws - mark.c.throws) / secs, (c.sched_events - mark.c.sched_events) / n,
            (t.requests - mark.t.requests) / n, (t.hits - mark.t.hits) / n,
            (t.decoded - mark.t.decoded) / n, (t.over_budget - mark.t.over_budget) / n,
            (t.rehashed - mark.t.rehashed) / n,
            ((t.hash_us + t.decode_us) - (mark.t.hash_us + mark.t.decode_us)) / 1000.0 / n,
            (draw_us - mark.draw_us) / 1000.0 / rn);
        mark.frame = f;
        mark.host_us = now;
        mark.renders = renders;
        mark.draw_us = draw_us;
        mark.c = c;
        mark.t = t;
    };
    pvr.on_render = [&](const std::vector<std::uint32_t>& stream) {
        const std::uint64_t r0 = host::clock_us();
        if (pvr.reg(0x060) & 0x01000000u) {
            ++texture_renders;
            texture_target = pvr.reg(0x060) & 0x00FFFFFFu;
        }
        // dibujar_desde: earlier renders are neither decoded nor drawn; the first drawn render
        // decodes every texture from the VRAM of that moment (docs/pruebas-rapidas.md §2).
        if (sys.spg.frames() < cfg.draw_from)
            return;
        decoder.reset();
        decoder.feed_stream(stream.data(), stream.size());
        dream::render::Frame& frame = decoder.frame();
        const std::size_t polygons = frame.polygon_count();
        const bool background = dream::render::add_background(frame, pvr.reg_block(),
            sys.memory.vram(), dream::mem::DcMemory::kVramSize);
        const float alpha_ref = static_cast<float>(pvr.reg(0x11C) & 0xFFu) / 255.0f;
        // Palette memory is at +0x1000 of the register block, its format in PAL_RAM_CTRL.
        host::texcache_begin_frame(sys.memory.vram(), dream::mem::DcMemory::kVramSize,
            pvr.reg_block() + 0x1000 / 4, pvr.reg(0x108) & 3u, sys.memory.vram_page_epoch,
            sys.memory.vram_write_epoch);
        ++sys.memory.vram_write_epoch;
        const bool last = cfg.capture_at && sys.spg.frames() >= cfg.capture_at;
        if (last && cfg.trace)
            host::video_set_trace(&log_line);
        dream::render::FogSettings fog;
        dream::render::describe_fog(pvr.reg_block(), fog);
        const host::VideoStats st = host::video_draw_frame(frame, alpha_ref, &fog);
        host::video_set_trace(nullptr);
        draw_us += host::clock_us() - r0;
        ++renders;
        if (cfg.memory)
            sample_memory();
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
            log_line("render: clip_inside %" PRIu32 " clip_outside %" PRIu32 " fogged %" PRIu32 " texture_renders %"
                PRIu64 " last_texture_target 0x%06" PRIx32 " fb_w_ctrl 0x%08" PRIx32
                " fb_w_sof1 0x%08" PRIx32, st.clip_inside, st.clip_outside, st.fogged, texture_renders,
                texture_target, pvr.reg(0x048), pvr.reg(0x060));
            const host::TextureCacheStats ts = host::texcache_stats();
            log_line("textures: textured %" PRIu32 " entries %" PRIu32 " bytes %" PRIu32
                " peak %" PRIu32 " budget %" PRIu32 " decoded %" PRIu32 " invalidated %" PRIu32
                " evicted %" PRIu32 " failed %" PRIu32 " over_budget %" PRIu32 " hash_ms %.1f"
                " decode_ms %.1f rejected_this_render %" PRIu32 " images_this_render %" PRIu32
                " demand_this_render %" PRIu32 " peak_demand %" PRIu32, st.textured, ts.entries, ts.bytes, ts.peak_bytes, ts.budget,
                ts.decoded, ts.invalidated, ts.evicted, ts.failed, ts.over_budget,
                ts.hash_us / 1000.0, ts.decode_us / 1000.0, ts.render_over_budget, ts.render_images, ts.render_demand, ts.peak_demand);
            {
                // Fog state of the captured render: registers and how many strips ask for each
                // fog mode (TSP bits 22-23) or carry an offset colour (PCW bit 2).
                unsigned modes[4] = {}, offset = 0;
                for (const auto& list : frame.lists)
                    for (const auto& p : list) {
                        ++modes[(p.tsp >> 22) & 3u];
                        offset += (p.pcw >> 2) & 1u;
                    }
                char table[16 * 5 + 1] = {};
                for (int i = 0; i < 16; ++i)
                    std::snprintf(table + i * 5, 6, "%04x ", pvr.reg(0x200 + i * 32) & 0xFFFFu);
                log_line("fog: density 0x%04" PRIx32 " col_ram 0x%06" PRIx32 " col_vert 0x%06"
                    PRIx32 " clamp %08" PRIx32 "-%08" PRIx32 " table/8 %s modes %u %u %u %u offset %u"
                    " zmin %g zmax %g", pvr.reg(0x0B8) & 0xFFFFu, pvr.reg(0x0B0) & 0xFFFFFFu,
                    pvr.reg(0x0B4) & 0xFFFFFFu, pvr.reg(0x0C0), pvr.reg(0x0BC), table, modes[0],
                    modes[1], modes[2], modes[3], offset, frame.min_z, frame.max_z);
            }
            log_perf(f);
            host::texcache_describe(&log_line);
            if (cfg.memory)
                log_memory_counter("capture", f);
            log_memory("memory");
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
        test_hooks(f, cfg.test_exit_at, cfg.test_suspend_at, *disc);
        check_exit();
        if (cfg.memory) {
            sample_memory();
            if (f % cfg.memory_every == 0)
                log_memory_counter("tick", f);
        }
        if (f % kLogEvery == 0) {
            report("tick");
            log_perf(f);
            log_line("video: renders %" PRIu64 " draw_ms_per_render %.2f texture_renders %" PRIu64,
                renders, renders ? static_cast<double>(draw_us) / 1000.0 / renders : 0.0,
                texture_renders);
            const host::TextureCacheStats ts = host::texcache_stats();
            log_line("textures: entries %" PRIu32 " bytes %" PRIu32 " peak %" PRIu32
                " decoded %" PRIu32 " invalidated %" PRIu32 " evicted %" PRIu32 " failed %" PRIu32
                " over_budget %" PRIu32 " hash_ms %.1f decode_ms %.1f", ts.entries, ts.bytes,
                ts.peak_bytes, ts.decoded, ts.invalidated, ts.evicted, ts.failed, ts.over_budget,
                ts.hash_us / 1000.0, ts.decode_us / 1000.0);
        }
        if (cfg.max_frames && f >= cfg.max_frames)
            throw StopRun{"max_cuadros reached"};
    };

    if (cfg.memory)
        log_memory_counter("setup", 0);
    log_line("start: %zu bytes of 1ST_READ.BIN at 0x%08" PRIx32 ", disc %u sectors",
        program.size(), kLoadAddress, disc->tracks().back().sectors);

    // Saved state at the head of kmWaitVBlank's wait loop, where run_guest_from can resume
    // (docs/estado-guardado.md). A turn with an interrupt already due is skipped: the resumed
    // function would take it before the poll where the original run took it.
    constexpr std::uint32_t kSavePc = 0x0C0370BAu;
    auto state_devices = [&](auto& io) {
        auto io1 = [&io](auto& dev) {
            if constexpr (std::is_same_v<std::decay_t<decltype(io)>, dream::state::Writer>)
                dev.save(io);
            else
                dev.load(io);
        };
        io1(pvr);
        io1(dmac);
        io1(sysblock);
        io1(g2);
        io1(g1);
        io1(pvrdma);
        io1(dma_triggers);
        io1(bsc);
        io1(ubc);
        io1(cpg);
        io1(sh4rtc);
        io1(scif);
        io1(maple);
        io1(aica);
        io1(rtc);
        io1(bios);
    };
    if (cfg.save_state_at) {
        sys.before_idle = [&, done = false](dream::sh4::Ctx& c, std::uint32_t pc) mutable {
            if (done || pc != kSavePc || sys.spg.frames() < cfg.save_state_at ||
                sys.nesting != 0 || c.cycles >= c.next_event)
                return;
            done = true;
            char name[48];
            std::snprintf(name, sizeof name, "estado-%" PRIu64 ".bin", cfg.save_state_at);
            const std::string path = path_in_dir(name);
            const std::uint64_t s0 = host::clock_us();
            std::FILE* f = std::fopen(path.c_str(), "wb");
            if (!f) {
                log_line("state: cannot write %s", path.c_str());
                return;
            }
            dream::state::Writer w(f);
            w.begin("HOST", 1);
            w.u32(pc);
            w.u64(sys.spg.frames());
            w.end();
            sys.save(w);
            state_devices(w);
            std::fclose(f);
            log_line("state: saved frame %" PRIu64 " at pc 0x%08" PRIx32 " to %s in %.2f s",
                sys.spg.frames(), pc, name, static_cast<double>(host::clock_us() - s0) / 1e6);
        };
    }
    const std::uint32_t stop_pr = sys.ctx.pr;
    std::uint32_t resume_pc = 0;
    if (!cfg.load_state.empty()) {
        const std::string path = path_in_dir(cfg.load_state.c_str());
        const std::uint64_t s0 = host::clock_us();
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) {
            log_line("state: cannot read %s", path.c_str());
            return quit(2);
        }
        try {
            dream::state::Reader r(f);
            r.begin("HOST", 1);
            resume_pc = r.u32();
            const std::uint64_t frame = r.u64();
            r.end();
            sys.load(r);
            state_devices(r);
            log_line("state: loaded frame %" PRIu64 " from %s in %.2f s, resuming at pc 0x%08"
                PRIx32, frame, cfg.load_state.c_str(),
                static_cast<double>(host::clock_us() - s0) / 1e6, resume_pc);
        } catch (const std::exception& e) {
            std::fclose(f);
            log_line("state: %s: %s", cfg.load_state.c_str(), e.what());
            return quit(2);
        }
        std::fclose(f);
    }

    int rc = 0;
    try {
        if (resume_pc)
            dream::sh4::run_guest_from(sys.ctx, sys.memory, resume_pc, stop_pr);
        else
            dream::sh4::run_guest(sys.ctx, sys.memory, kEntry);
        report("stop: guest returned");
    } catch (const ExitRequested&) {
        report("stop: exit requested");
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
