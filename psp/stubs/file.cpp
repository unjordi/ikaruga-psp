#include <pspiofilemgr.h>

#include "host.h"

namespace ikaruga::host {

bool file_read(const char* path, std::vector<std::uint8_t>& out) {
    out.clear();
    const SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return false;
    const SceOff size = sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    if (size < 0) {
        sceIoClose(fd);
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    std::size_t done = 0;
    while (done < out.size()) {
        const int n = sceIoRead(fd, out.data() + done, out.size() - done);
        if (n <= 0)
            break;
        done += static_cast<std::size_t>(n);
    }
    sceIoClose(fd);
    if (done != out.size()) {
        out.clear();
        return false;
    }
    return true;
}

bool file_write(const char* path, const std::uint8_t* data, std::uint32_t size) {
    const SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0)
        return false;
    const int n = sceIoWrite(fd, data, size);
    sceIoClose(fd);
    return n == static_cast<int>(size);
}

bool file_append(const char* path, const char* text, std::uint32_t size) {
    const SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd < 0)
        return false;
    const int n = sceIoWrite(fd, text, size);
    sceIoClose(fd);
    return n == static_cast<int>(size);
}

}  // namespace ikaruga::host
