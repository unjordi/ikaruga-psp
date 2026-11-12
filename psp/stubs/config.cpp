// Reader of ikaruga.cfg: the one parser behind main.cpp's keys and input.cpp's mapping.
#include <string>

#include "host.h"

namespace ikaruga::host {

namespace {

std::string trimmed(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos)
        return {};
    return s.substr(a, s.find_last_not_of(" \t\r") + 1 - a);
}

}  // namespace

std::vector<CfgEntry> cfg_read(const char* path) {
    std::vector<CfgEntry> entries;
    std::vector<std::uint8_t> raw;
    if (!file_read(path, raw))
        return entries;
    const std::string text(raw.begin(), raw.end());
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        line = line.substr(0, line.find('#'));
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        entries.push_back({trimmed(line.substr(0, eq)), trimmed(line.substr(eq + 1))});
    }
    return entries;
}

}  // namespace ikaruga::host
