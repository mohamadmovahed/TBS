#pragma once

#include "export.h"

#include <cstdint>
#include <string>

namespace tbs {

// Human readable names for the linux-dvb enum values reported in FrontendStatus.
// The tables follow the TBS linux_media headers (mainline enums + DVB-S2X additions).
namespace DvbNames {

TBSDVB_API std::string deliverySystem(int v);
TBSDVB_API std::string modulation(int v);
TBSDVB_API std::string fec(int v);
TBSDVB_API std::string rolloff(int v);
TBSDVB_API std::string pilot(int v);
TBSDVB_API std::string statusFlags(uint32_t flags);

} // namespace DvbNames
} // namespace tbs
