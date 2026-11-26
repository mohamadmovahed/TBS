// Non-standard generic stream decoders: GSE dialects, TS / CRC-8 TS in a generic
// stream, raw and byte-swapped IPv4, and the auto detection that ranks them.
//
// Record layout: flags(1) syncd(2) len(2) | data field
//   flags bit 0: HEM; bits 1-2 of the first record: GSE dialect

#include "FuzzInput.h"

#include <tbsbbproc/GenericDecoders.h>
#include <tbsbbproc/IpStatistics.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    FuzzInput in(data, size);
    tbs::bb::GenericDecoderProbe probe;
    tbs::bb::GseDecapsulator gse;
    tbs::bb::RawIpExtractor ip(false), ipSwapped(true);
    tbs::bb::GenericTsExtractor ts(false), crcTs(true);
    tbs::bb::IpStatistics stats(64, 16);
    size_t tsPackets = 0;
    int fields = 0;
    int64_t t = 0;
    auto onPdu = [&](const tbs::bb::GsePdu &pdu) { stats.addPdu(pdu, t); };
    auto onTs = [&](const uint8_t *pkt) { tsPackets += pkt[0] == 0x47; };
    while (in.remaining() >= 5 && fields < 64) {
        const uint8_t flags = in.u8();
        const uint16_t syncd = in.u16();
        const std::vector<uint8_t> df = in.bytes(in.u16() & 0x1FFF);
        if (fields == 0) {
            tbs::bb::GseDialect d;
            d.lengthIncludesHeader = flags & 2;
            d.splitFragId = flags & 4;
            gse.setDialect(d);
        }
        const bool hem = flags & 1;
        probe.pushDataField(df.data(), df.size(), hem, syncd);
        gse.pushDataField(df.data(), df.size(), hem, syncd, onPdu);
        ip.pushDataField(df.data(), df.size(), onPdu);
        ipSwapped.pushDataField(df.data(), df.size(), onPdu);
        ts.pushDataField(df.data(), df.size(), onTs);
        crcTs.pushDataField(df.data(), df.size(), onTs);
        if (flags & 0x80)
            (void)probe.conclusive();
        t += 10;
        ++fields;
    }
    (void)probe.scores();
    (void)probe.best();
    (void)stats.report(100, t + 1000);
    return tsPackets == static_cast<size_t>(-1); // keep the counter alive
}
