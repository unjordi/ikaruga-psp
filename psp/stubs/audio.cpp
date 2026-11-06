#include "host.h"

namespace ikaruga::host {

bool audio_init(unsigned) { return false; }

void audio_push(std::int16_t, std::int16_t) {}

}  // namespace ikaruga::host
