// GSE decapsulator (normal mode and HEM with SYNCD), extension headers,
// IP statistics and the pcap writer.
//
// Record layout: flags(1) syncd(2) len(2) | data field
// (flags bit 0: HEM; bits 1-2 of the first record: GSE dialect)

#include "FuzzInput.h"

#include <tbsbbproc/Gse.h>
#include <tbsbbproc/IpStatistics.h>
#include <tbsbbproc/PcapWriter.h>

#include <string>
#include <unistd.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static tbs::bb::PcapWriter *pcapEth = nullptr;
    static tbs::bb::PcapWriter *pcapRaw = nullptr;
    if (!pcapEth) {
        tbs::bb::PcapOptions o;
        o.enabled = true;
        o.timestampSuffix = false;
        o.ipOnly = false;
        o.path = "/tmp/tbs_fuzz_gse_" + std::to_string(getpid()) + "_eth.pcap";
        pcapEth = new tbs::bb::PcapWriter(o);
        pcapEth->open(nullptr);
        o.ethernetFraming = false;
        o.path = "/tmp/tbs_fuzz_gse_" + std::to_string(getpid()) + "_raw.pcap";
        pcapRaw = new tbs::bb::PcapWriter(o);
        pcapRaw->open(nullptr);
    }

    FuzzInput in(data, size);
    tbs::bb::GseDecapsulator gse;
    tbs::bb::IpStatistics ip(64, 16);
    int64_t t = 0;
    int fields = 0;
    while (in.remaining() >= 5 && fields < 64) {
        const uint8_t flags = in.u8();
        const uint16_t syncd = in.u16();
        const std::vector<uint8_t> df = in.bytes(in.u16() & 0x1FFF);
        if (fields == 0) {
            // bits 1-2 of the first record: non-standard dialect
            tbs::bb::GseDialect d;
            d.lengthIncludesHeader = flags & 2;
            d.splitFragId = flags & 4;
            gse.setDialect(d);
        }
        gse.pushDataField(df.data(), df.size(), flags & 1, syncd, [&](const tbs::bb::GsePdu &pdu) {
            ip.addPdu(pdu, t);
            pcapEth->write(pdu, t * 1000);
            pcapRaw->write(pdu, t * 1000);
        });
        t += 10;
        if (flags & 0x80)
            (void)ip.report(8, t);
        ++fields;
    }
    (void)ip.report(100, t + 1000);
    return 0;
}
