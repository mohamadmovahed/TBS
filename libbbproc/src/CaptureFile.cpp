#include "tbsbbproc/CaptureFile.h"

#include <tbsdvb/BbFrameExtractor.h>
#include <tbsdvb/TsAnalyzer.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace tbs {
namespace bb {

namespace {

std::FILE *openRead(const std::string &path)
{
#if defined(_WIN32)
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], n);
    return _wfopen(w.c_str(), L"rb");
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

bool validHeaderAt(const std::vector<uint8_t> &b, size_t pos)
{
    if (pos + tbs::BbHeader::kSize > b.size())
        return false;
    tbs::BbHeader h;
    return h.parse(b.data() + pos) && h.dfl > 0;
}

// Raw BBFrames that do not start at byte 0 (a cut recording, or a capture with
// stray bytes between the frames): two headers in a row, at most kGap bytes apart
bool rawFramesSomewhere(const std::vector<uint8_t> &b, size_t limit)
{
    constexpr size_t kGap = 4;
    const size_t end = std::min(b.size(), limit);
    for (size_t pos = 0; pos + tbs::BbHeader::kSize <= end; ++pos) {
        tbs::BbHeader h;
        if (!h.parse(b.data() + pos) || h.dfl == 0)
            continue;
        const size_t next = pos + tbs::BbHeader::kSize + h.dataFieldBytes();
        for (size_t g = 0; g <= kGap; ++g)
            if (validHeaderAt(b, next + g))
                return true;
    }
    return false;
}

} // namespace

const char *captureFormatName(CaptureFormat f)
{
    switch (f) {
    case CaptureFormat::Unknown:
        return "unknown";
    case CaptureFormat::TransportStream:
        return "transport stream";
    case CaptureFormat::PseudoTs:
        return "pseudo-TS (BBFrames in PID 0x010E/0x0118)";
    case CaptureFormat::RawBbFrames:
        return "raw BBFrames";
    case CaptureFormat::B8BbFrames:
        return "0xB8 + BBFrames";
    }
    return "";
}

bool readCaptureFile(const std::string &path, const CaptureCallbacks &cb, CaptureFormat *format, std::string *error)
{
    std::FILE *f = openRead(path);
    if (!f) {
        if (error)
            *error = "Cannot open '" + path + "': " + std::strerror(errno);
        return false;
    }

    std::vector<uint8_t> buf;
    std::vector<uint8_t> chunk(1u << 20);
    auto fill = [&]() {
        const size_t n = std::fread(chunk.data(), 1, chunk.size(), f);
        buf.insert(buf.end(), chunk.data(), chunk.data() + n);
        return n > 0;
    };
    fill();

    // ---- Detect the format ----
    CaptureFormat fmt = CaptureFormat::Unknown;
    if (buf.size() >= 188 * 3 && buf[0] == 0x47 && buf[188] == 0x47 && buf[376] == 0x47) {
        fmt = CaptureFormat::TransportStream; // may turn out to be pseudo-TS below
    } else if (!buf.empty() && buf[0] == 0xB8 && validHeaderAt(buf, 1)) {
        fmt = CaptureFormat::B8BbFrames;
    } else if (validHeaderAt(buf, 0) || rawFramesSomewhere(buf, 1u << 16)) {
        fmt = CaptureFormat::RawBbFrames; // the reader resynchronises on each header
    }
    if (fmt == CaptureFormat::Unknown) {
        std::fclose(f);
        if (error)
            *error = "Unrecognised capture format (expected TS, pseudo-TS or BBFrames)";
        return false;
    }

    if (fmt == CaptureFormat::TransportStream) {
        tbs::TsAnalyzer ts;
        tbs::BbFrameExtractor bbx;
        bool pseudo = false;
        bool decided = false;
        std::vector<uint8_t> pending; // packets held until the stream type is known
        auto onPackets = [&](const uint8_t *pk, size_t count) {
            for (size_t i = 0; i < count; ++i) {
                const uint8_t *p = pk + i * 188;
                if (!decided) {
                    if (tbs::BbFrameExtractor::isBbFramePid(tbs::TsAnalyzer::pidOf(p))) {
                        pseudo = true;
                        decided = true;
                    } else {
                        pending.insert(pending.end(), p, p + 188);
                        if (pending.size() >= 188 * 2000)
                            decided = true; // plenty of packets and no BBFrame PID: plain TS
                        else
                            continue;
                    }
                    // Flush what was held back
                    if (!pseudo && cb.onTs && !pending.empty())
                        cb.onTs(pending.data(), pending.size() / 188);
                    if (pseudo)
                        for (size_t k = 0; k + 188 <= pending.size(); k += 188)
                            bbx.pushPacket(pending.data() + k, cb.onBbFrame);
                    pending.clear();
                    if (!pseudo)
                        continue;
                }
                if (pseudo)
                    bbx.pushPacket(p, cb.onBbFrame);
                else if (cb.onTs)
                    cb.onTs(p, 1);
            }
        };
        do {
            ts.push(buf.data(), buf.size(), onPackets);
            buf.clear();
        } while (fill());
        if (!decided && !pending.empty() && cb.onTs)
            cb.onTs(pending.data(), pending.size() / 188);
        if (format)
            *format = pseudo ? CaptureFormat::PseudoTs : CaptureFormat::TransportStream;
        std::fclose(f);
        return true;
    }

    // ---- Raw / B8 BBFrames ----
    const size_t prefix = fmt == CaptureFormat::B8BbFrames ? 1 : 0;
    size_t pos = 0;
    bool eof = false;
    while (true) {
        if (buf.size() - pos < (1u << 16) && !eof) {
            buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(pos));
            pos = 0;
            eof = !fill();
        }
        if (pos + prefix + tbs::BbHeader::kSize > buf.size())
            break;
        tbs::BbHeader h;
        const bool markerOk = prefix == 0 || buf[pos] == 0xB8;
        if (!markerOk || !h.parse(buf.data() + pos + prefix)) {
            ++pos; // resynchronise on the next valid header
            continue;
        }
        const size_t frameLen = tbs::BbHeader::kSize + h.dataFieldBytes();
        if (pos + prefix + frameLen > buf.size()) {
            if (eof)
                break;
            continue;
        }
        if (cb.onBbFrame)
            cb.onBbFrame(buf.data() + pos + prefix, frameLen, h);
        pos += prefix + frameLen;
    }
    if (format)
        *format = fmt;
    std::fclose(f);
    return true;
}

} // namespace bb
} // namespace tbs
