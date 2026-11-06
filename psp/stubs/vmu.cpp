#include <pspiofilemgr.h>

#include "host.h"

namespace ikaruga::host {

bool vmu_load(const char* path, dream::maple::MemoryCard& card) {
    SceIoStat st{};
    if (sceIoGetstat(path, &st) >= 0 && card.load(path) == dream::maple::CardStatus::Ok)
        return true;
    card.format();
    return false;
}

}  // namespace ikaruga::host
