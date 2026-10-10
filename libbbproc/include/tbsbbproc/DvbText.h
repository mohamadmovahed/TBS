#pragma once

#include "export.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace tbs {
namespace bb {

// Converts a DVB SI string (EN 300 468 Annex A) to UTF-8. Handles the
// character table selector (ISO 8859-1/2/5/6/7/8/9/15, UTF-8, UCS-2) and
// the DVB control codes; the default table (ISO 6937) is approximated.
TBSBBPROC_API std::string dvbTextToUtf8(const uint8_t *p, size_t len);

// MPEG-2 CRC-32 (polynomial 0x04C11DB7, initial value 0xFFFFFFFF, no reflection)
TBSBBPROC_API uint32_t crc32Mpeg(const uint8_t *p, size_t len, uint32_t crc = 0xFFFFFFFFu);

} // namespace bb
} // namespace tbs
