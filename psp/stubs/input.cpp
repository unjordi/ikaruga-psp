// Row 5: no PSP buttons yet. Scripted presses (the same "start@700,a@1400" syntax as the PC
// launcher's --press) are held for 8 guest frames each, active low as the Maple bus expects.
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "host.h"

namespace ikaruga::host {

namespace {
std::vector<std::pair<std::uint64_t, std::uint16_t>> g_script;
}

std::size_t input_script(const std::string& presses) {
    static const struct {
        const char* name;
        std::uint16_t bit;
    } kNames[] = {{"c", dream::maple::kC},       {"b", dream::maple::kB},
                  {"a", dream::maple::kA},       {"start", dream::maple::kStart},
                  {"up", dream::maple::kUp},     {"down", dream::maple::kDown},
                  {"left", dream::maple::kLeft}, {"right", dream::maple::kRight},
                  {"z", dream::maple::kZ},       {"y", dream::maple::kY},
                  {"x", dream::maple::kX}};
    g_script.clear();
    std::string rest = presses;
    while (!rest.empty()) {
        const std::size_t comma = rest.find(',');
        const std::string one = rest.substr(0, comma);
        rest = comma == std::string::npos ? std::string() : rest.substr(comma + 1);
        const std::size_t at = one.find('@');
        if (at == std::string::npos)
            continue;
        const std::string name = one.substr(0, at);
        const std::uint64_t frame = std::strtoull(one.c_str() + at + 1, nullptr, 0);
        for (const auto& n : kNames)
            if (name == n.name)
                g_script.push_back({frame, n.bit});
    }
    return g_script.size();
}

void input_poll(dream::maple::ControllerState& pad, std::uint64_t frame) {
    pad = dream::maple::ControllerState{};
    std::uint16_t held = 0;
    for (const auto& [start, bit] : g_script)
        if (frame >= start && frame < start + 8)
            held |= bit;
    pad.buttons = static_cast<std::uint16_t>(0xFFFFu & ~held);
}

}  // namespace ikaruga::host
