// Controller: the PSP's buttons and analog stick become a Dreamcast pad on the Maple bus, OR'd
// with scripted presses (the same "start@700,a@1400" syntax as the PC launcher's --press), each
// held for 8 guest frames. Maple buttons are active low; the triggers are 0 (released) to 255.
//
// The mapping comes from ikaruga.cfg (see input_config and docs/controles-psp.md): each PSP
// button names the Dreamcast control it presses, the stick acts as a d-pad past a threshold, and
// TATE mode turns the d-pad and the stick 90 degrees for a console held on its side.
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <pspctrl.h>

#include "host.h"

namespace ikaruga::host {

namespace {

// What a PSP button presses on the Dreamcast pad: a button bit, or a whole trigger.
struct Target {
    std::uint16_t bit = 0;
    bool ltrigger = false, rtrigger = false;
};

struct Binding {
    const char* key;      // ikaruga.cfg key
    unsigned psp;         // PSP_CTRL_* bit
    Target target;
};

// Defaults: the Dreamcast pad's own layout (docs/controles-psp.md gives the source for what
// each button does in Ikaruga): Cross = A, Circle = B, Square = X, Triangle = Y.
Binding g_bindings[] = {
    {"boton_cruz", PSP_CTRL_CROSS, {dream::maple::kA}},
    {"boton_circulo", PSP_CTRL_CIRCLE, {dream::maple::kB}},
    {"boton_cuadrado", PSP_CTRL_SQUARE, {dream::maple::kX}},
    {"boton_triangulo", PSP_CTRL_TRIANGLE, {dream::maple::kY}},
    {"boton_l", PSP_CTRL_LTRIGGER, {0, true, false}},
    {"boton_r", PSP_CTRL_RTRIGGER, {0, false, true}},
    {"boton_start", PSP_CTRL_START, {dream::maple::kStart}},
    {"boton_select", PSP_CTRL_SELECT, {}},
};

std::vector<std::pair<std::uint64_t, std::uint16_t>> g_script;
bool g_enabled = true;  // control=0 leaves only the scripted presses
int g_threshold = 64;   // stick distance from centre (0-127) that counts as a d-pad press
int g_tate = 0;         // 0 upright; 1 console turned counter-clockwise; 2 clockwise
bool g_ready = false;

bool parse_target(const std::string& name, Target& out) {
    static const struct {
        const char* name;
        std::uint16_t bit;
    } kNames[] = {{"c", dream::maple::kC},       {"b", dream::maple::kB},
                  {"a", dream::maple::kA},       {"start", dream::maple::kStart},
                  {"up", dream::maple::kUp},     {"down", dream::maple::kDown},
                  {"left", dream::maple::kLeft}, {"right", dream::maple::kRight},
                  {"z", dream::maple::kZ},       {"y", dream::maple::kY},
                  {"x", dream::maple::kX},       {"d", dream::maple::kD}};
    out = Target{};
    if (name == "l")
        out.ltrigger = true;
    else if (name == "r")
        out.rtrigger = true;
    else if (name == "nada" || name == "none")
        return true;
    else {
        for (const auto& n : kNames)
            if (name == n.name) {
                out.bit = n.bit;
                return true;
            }
        return false;
    }
    return true;
}

// Turns an upright d-pad direction set into the one the player means with the console on its
// side. Counter-clockwise: the PSP's right edge is now the top, so right means up.
std::uint16_t rotate_dpad(std::uint16_t dirs, int tate) {
    if (tate == 0)
        return dirs;
    using namespace dream::maple;
    std::uint16_t out = 0;
    const bool ccw = tate == 1;
    if (dirs & kRight) out |= ccw ? kUp : kDown;
    if (dirs & kUp) out |= ccw ? kLeft : kRight;
    if (dirs & kLeft) out |= ccw ? kDown : kUp;
    if (dirs & kDown) out |= ccw ? kRight : kLeft;
    return out;
}

void init_ctrl() {
    if (g_ready)
        return;
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    g_ready = true;
}

}  // namespace

void input_config(const std::vector<CfgEntry>& cfg) {
    for (const CfgEntry& e : cfg) {
        const std::string& key = e.key;
        const std::string& value = e.value;
        if (key == "control")
            g_enabled = value != "0";
        else if (key == "umbral_stick")
            g_threshold = static_cast<int>(std::strtol(value.c_str(), nullptr, 0));
        else if (key == "tate")
            g_tate = value == "1" || value == "antihorario" ? 1 : value == "2" || value == "horario" ? 2 : 0;
        else
            for (auto& b : g_bindings)
                if (key == b.key) {
                    Target t;
                    if (parse_target(value, t))
                        b.target = t;
                }
    }
}

std::size_t input_script(const std::string& presses) {
    g_script.clear();
    std::string rest = presses;
    while (!rest.empty()) {
        const std::size_t comma = rest.find(',');
        const std::string one = rest.substr(0, comma);
        rest = comma == std::string::npos ? std::string() : rest.substr(comma + 1);
        const std::size_t at = one.find('@');
        if (at == std::string::npos)
            continue;
        Target t;
        if (!parse_target(one.substr(0, at), t) || t.bit == 0)
            continue;
        g_script.push_back({std::strtoull(one.c_str() + at + 1, nullptr, 0), t.bit});
    }
    return g_script.size();
}

void input_poll(dream::maple::ControllerState& pad, std::uint64_t frame) {
    pad = dream::maple::ControllerState{};
    std::uint16_t held = 0;
    for (const auto& [start, bit] : g_script)
        if (frame >= start && frame < start + 8)
            held |= bit;
    if (g_enabled) {
        init_ctrl();
        SceCtrlData data{};
        sceCtrlPeekBufferPositive(&data, 1);
        std::uint16_t dirs = 0;
        if (data.Buttons & PSP_CTRL_UP) dirs |= dream::maple::kUp;
        if (data.Buttons & PSP_CTRL_DOWN) dirs |= dream::maple::kDown;
        if (data.Buttons & PSP_CTRL_LEFT) dirs |= dream::maple::kLeft;
        if (data.Buttons & PSP_CTRL_RIGHT) dirs |= dream::maple::kRight;
        const int dx = static_cast<int>(data.Lx) - 128, dy = static_cast<int>(data.Ly) - 128;
        if (dx <= -g_threshold) dirs |= dream::maple::kLeft;
        if (dx >= g_threshold) dirs |= dream::maple::kRight;
        if (dy <= -g_threshold) dirs |= dream::maple::kUp;
        if (dy >= g_threshold) dirs |= dream::maple::kDown;
        held |= rotate_dpad(dirs, g_tate);
        for (const auto& b : g_bindings) {
            if (!(data.Buttons & b.psp))
                continue;
            held |= b.target.bit;
            if (b.target.ltrigger)
                pad.ltrigger = 0xFF;
            if (b.target.rtrigger)
                pad.rtrigger = 0xFF;
        }
    }
    pad.buttons = static_cast<std::uint16_t>(0xFFFFu & ~held);
}

}  // namespace ikaruga::host
