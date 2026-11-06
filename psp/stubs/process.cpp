#include <psppower.h>

#include "host.h"

namespace ikaruga::host {

// Row 13: run the Allegrex at its maximum (CPU 333 MHz, bus 166 MHz).
void process_init() { scePowerSetClockFrequency(333, 333, 166); }

}  // namespace ikaruga::host
