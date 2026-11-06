// Row 8: the GD-ROM served from an ISO 9660 image of the game area on ms0:, read with sceIo.
// The image is built on the host (scripts/preparar-disco-ikaruga.sh) with its sector numbers
// starting at LBA 45000 and IP.BIN in its system area, so its sector N is the disc's 45000+N.
// Tracks 1 and 2 (the single-density area) are only described for the TOC and read as zeros.
#include <pspiofilemgr.h>

#include <cstring>
#include <memory>
#include <string>

#include "host.h"

namespace ikaruga::host {

namespace {

class Ms0Disc final : public dream::gdrom::Disc {
public:
    ~Ms0Disc() override {
        if (fd_ >= 0)
            sceIoClose(fd_);
    }

    bool open(const char* path, std::string& error) {
        path_ = path;
        fd_ = sceIoOpen(path, PSP_O_RDONLY, 0);
        if (fd_ < 0) {
            error = std::string("cannot open ") + path;
            return false;
        }
        const SceOff size = sceIoLseek(fd_, 0, PSP_SEEK_END);
        if (size < static_cast<SceOff>(16 * dream::gdrom::kUserBytes)) {
            error = std::string("too small for a game area: ") + path;
            return false;
        }
        // Layout of a GD-ROM: data track 1 and audio track 2 in the single-density area, the game
        // in track 3 from LBA 45000.
        dream::gdrom::Track t1;
        t1.number = 1;
        t1.lba = 0;
        t1.sectors = 300;
        t1.sector_size = 2048;
        t1.data = true;
        dream::gdrom::Track t2;
        t2.number = 2;
        t2.lba = 450;
        t2.sectors = 300;
        t2.sector_size = 2352;
        dream::gdrom::Track t3;
        t3.number = 3;
        t3.lba = dream::gdrom::kHdAreaLba;
        t3.sectors = static_cast<std::uint32_t>(size / dream::gdrom::kUserBytes);
        t3.sector_size = 2048;
        t3.data = true;
        tracks_ = {t1, t2, t3};
        finish_tracks();
        return true;
    }

    bool read_raw(std::uint32_t lba, std::uint8_t* out) override {
        const dream::gdrom::Track* t = track_for(lba);
        if (!t)
            return false;
        if (t->number != 3) {
            std::memset(out, 0, t->sector_size);
            return true;
        }
        const SceOff at = static_cast<SceOff>(lba - t->lba) * dream::gdrom::kUserBytes;
        if (at != pos_ && sceIoLseek(fd_, at, PSP_SEEK_SET) != at)
            return false;
        const int n = sceIoRead(fd_, out, dream::gdrom::kUserBytes);
        if (n != static_cast<int>(dream::gdrom::kUserBytes)) {
            pos_ = -1;
            return false;
        }
        pos_ = at + n;
        return true;
    }

private:
    SceUID fd_ = -1;
    SceOff pos_ = -1;
};

}  // namespace

std::unique_ptr<dream::gdrom::Disc> disc_open(const char* path, std::string& error) {
    auto d = std::make_unique<Ms0Disc>();
    if (!d->open(path, error))
        return nullptr;
    return d;
}

}  // namespace ikaruga::host
