#include "host.h"

namespace ikaruga::host {

bool file_read(const char*, std::vector<std::uint8_t>& out) {
    out.clear();
    return false;
}

bool file_write(const char*, const std::uint8_t*, std::uint32_t) { return false; }

}  // namespace ikaruga::host
