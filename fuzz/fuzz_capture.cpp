// Capture file reader: format detection and BBFrame resynchronisation for
// raw / 0xB8 / pseudo-TS / TS recordings.

#include <tbsbbproc/CaptureFile.h>
#include <tbsbbproc/Processor.h>

#include <cstdio>
#include <string>
#include <unistd.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const std::string path = "/tmp/tbs_fuzz_capture_" + std::to_string(getpid()) + ".bin";
    std::FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return 0;
    std::fwrite(data, 1, size, f);
    std::fclose(f);

    tbs::bb::Processor proc;
    tbs::bb::CaptureCallbacks cb;
    cb.onBbFrame = [&](const uint8_t *fr, size_t n, const tbs::BbHeader &h) { proc.pushBbFrame(fr, n, h); };
    cb.onTs = [&](const uint8_t *p, size_t n) { proc.pushTs(p, n); };
    tbs::bb::CaptureFormat fmt;
    std::string err;
    (void)tbs::bb::readCaptureFile(path, cb, &fmt, &err);
    (void)proc.snapshot();
    return 0;
}
