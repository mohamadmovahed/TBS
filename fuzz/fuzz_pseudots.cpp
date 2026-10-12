// Raw bytes as a (pseudo) transport stream: TS alignment, BBFrame reassembly
// from PID 0x010E/0x0118, BBFrame->TS recovery and the PSI/SI analyser on the TS.

#include <tbsbbproc/Processor.h>
#include <tbsbbproc/TsTableAnalyzer.h>
#include <tbsdvb/BbFrameExtractor.h>
#include <tbsdvb/BbFrameToTs.h>
#include <tbsdvb/TsAnalyzer.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tbs::TsAnalyzer ts;
    tbs::BbFrameExtractor bbx;
    tbs::BbFrameToTs bb2ts;
    tbs::bb::TsTableAnalyzer psi;
    tbs::bb::Processor proc;

    auto onFrame = [&](const uint8_t *f, size_t n, const tbs::BbHeader &h) {
        proc.pushBbFrame(f, n, h);
        bb2ts.pushFrame(f, n, h, [&](const uint8_t *pkt) { psi.pushPackets(pkt, 1); });
    };
    // Feed in two uneven pieces to exercise re-alignment across pushes
    const size_t split = size / 3;
    for (const auto &part : {std::make_pair(data, split), std::make_pair(data + split, size - split)}) {
        ts.push(part.first, part.second, [&](const uint8_t *pk, size_t count) {
            psi.pushPackets(pk, count);
            proc.pushTs(pk, count);
            for (size_t i = 0; i < count; ++i)
                bbx.pushPacket(pk + i * 188, onFrame);
        });
    }
    (void)psi.report();
    (void)ts.pidSnapshot();
    (void)tbs::bb::formatReport(proc.snapshot(), true);
    return 0;
}
