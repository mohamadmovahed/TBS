// BBFrames -> tbs::bb::Processor (stream detection, TS recovery + PSI/SI,
// GSE decapsulation, IP statistics, reports). The harness computes a valid
// BBHeader CRC so the fuzzer reaches the payload parsers.
//
// Record layout: flags(1) matype1 matype2 upl(2) dfl(2) sync syncd(2) | data field

#include "FuzzInput.h"

#include <tbsbbproc/Processor.h>
#include <tbsdvb/BbHeader.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    FuzzInput in(data, size);
    tbs::bb::Processor proc;
    int frames = 0;
    while (in.remaining() >= 10 && frames < 64) {
        const uint8_t flags = in.u8();
        std::vector<uint8_t> f = in.bytes(9);
        f.resize(10);
        f[9] = static_cast<uint8_t>(tbs::BbHeader::crc8(f.data(), 9) ^ (flags & 1)); // NM or HEM
        tbs::BbHeader h;
        if (!h.parse(f.data()))
            continue;
        // Data field: DFL bytes, or what the input has left
        const std::vector<uint8_t> df = in.bytes(std::min<size_t>(h.dataFieldBytes(), 7274));
        f.insert(f.end(), df.begin(), df.end());
        proc.pushBbFrame(f.data(), f.size(), h);
        if (flags & 0x80)
            (void)proc.snapshot();
        ++frames;
    }
    const tbs::bb::ProcessorSnapshot s = proc.snapshot();
    (void)tbs::bb::formatReport(s, true);
    return 0;
}
