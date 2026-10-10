#pragma once

#include "export.h"

#include <tbsdvb/BbHeader.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace tbs {
namespace bb {

enum class CaptureFormat { Unknown, TransportStream, PseudoTs, RawBbFrames, B8BbFrames };

TBSBBPROC_API const char *captureFormatName(CaptureFormat f);

struct TBSBBPROC_API CaptureCallbacks
{
    std::function<void(const uint8_t *frame, size_t len, const tbs::BbHeader &hdr)> onBbFrame;
    std::function<void(const uint8_t *packets, size_t count)> onTs; // plain TS captures
};

// Reads a recording made by tbs-receiver / tbs-cli (or any tool producing the same
// formats) and delivers BBFrames or TS packets. The format is detected:
//   TS / pseudo-TS (0x47 every 188 bytes; BBFrames on PID 0x010E/0x0118),
//   0xB8-prefixed BBFrames (bbframe-tools), raw BBFrames (BBHeader + DFL/8 bytes).
TBSBBPROC_API bool readCaptureFile(const std::string &path, const CaptureCallbacks &cb, CaptureFormat *format,
                                   std::string *error);

} // namespace bb
} // namespace tbs
