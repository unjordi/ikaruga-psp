// The memory card image on ms0:. Read whole at boot; every block write the guest makes
// rewrites the whole 128 KB image to <path>.TMP, then replaces <path> with it, so a power cut
// leaves either the old card or the new one, never half of each.
#include <pspiofilemgr.h>

#include <string>
#include <vector>

#include "host.h"

namespace ikaruga::host {

namespace {

bool replace_file(const std::string& path, const std::vector<std::uint8_t>& data) {
    const std::string tmp = path + ".TMP";
    if (!file_write(tmp.c_str(), data.data(), static_cast<std::uint32_t>(data.size())))
        return false;
    sceIoRemove(path.c_str());
    if (sceIoRename(tmp.c_str(), path.c_str()) < 0)
        return false;
    // Device name up to and including the colon ("ms0:", "ef0:").
    const std::size_t colon = path.find(':');
    if (colon != std::string::npos)
        sceIoSync(path.substr(0, colon + 1).c_str(), 0);
    return true;
}

}  // namespace

bool vmu_load(const char* path, dream::maple::MemoryCard& card) {
    const std::string where = path;
    card.writer = [where](const std::vector<std::uint8_t>& image) {
        return replace_file(where, image);
    };
    std::vector<std::uint8_t> raw;
    if (file_read(path, raw) && card.set_image(raw))
        return true;
    card.format();
    return false;
}

}  // namespace ikaruga::host
