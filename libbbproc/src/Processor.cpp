#include "tbsbbproc/Processor.h"

#include "tbsbbproc/TsTableAnalyzer.h"

#include <tbsdvb/BbFrameToTs.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>

namespace tbs {
namespace bb {

namespace {
int64_t steadyMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
int64_t unixMicros()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
constexpr int kTsInputKey = -2;
} // namespace

const char *streamContentName(StreamContent c)
{
    switch (c) {
    case StreamContent::Unknown:
        return "Unknown";
    case StreamContent::TransportStream:
        return "Transport stream";
    case StreamContent::Gse:
        return "GSE (IP)";
    case StreamContent::GenericContinuous:
        return "Generic continuous (not GSE)";
    case StreamContent::GenericPacketized:
        return "Generic packetized";
    case StreamContent::TsInput:
        return "Transport stream";
    }
    return "";
}

struct Processor::Impl
{
    struct Stream
    {
        explicit Stream(size_t maxFlows) : ip(maxFlows) {}
        int key = -1;
        uint64_t frames = 0;
        uint64_t dataBytes = 0;
        uint64_t dataBytesAtSnapshot = 0;
        bool hasHeader = false;
        tbs::BbHeader header;
        bool sawTsType = false;
        bool sawGpType = false;
        tbs::BbFrameToTs bb2ts;
        std::vector<uint8_t> tsBuf;
        TsTableAnalyzer psi;
        GseDecapsulator gse;
        IpStatistics ip;
    };

    ProcessorConfig cfg;
    std::mutex mutex;
    std::map<int, std::unique_ptr<Stream>> streams;
    std::unique_ptr<PcapWriter> pcap;
    std::string pcapError;
    int64_t startMs = steadyMs();
    int64_t lastSnapshotMs = 0;

    Stream &stream(int key)
    {
        auto &s = streams[key];
        if (!s) {
            s = std::make_unique<Stream>(cfg.maxFlows);
            s->key = key;
        }
        return *s;
    }

    void onPdu(Stream &s, const GsePdu &pdu)
    {
        s.ip.addPdu(pdu, steadyMs());
        if (pcap && pcap->isOpen()) {
            pcap->write(pdu, unixMicros());
            if (!pcap->lastError().empty() && pcapError.empty())
                pcapError = pcap->lastError();
        }
    }
};

Processor::Processor(const ProcessorConfig &cfg)
    : d(std::make_unique<Impl>())
{
    d->cfg = cfg;
}

Processor::~Processor()
{
    close();
}

bool Processor::open(std::string *error)
{
    std::lock_guard<std::mutex> lock(d->mutex);
    if (!d->cfg.pcap.enabled)
        return true;
    d->pcap = std::make_unique<PcapWriter>(d->cfg.pcap);
    if (!d->pcap->open(error)) {
        d->pcapError = error ? *error : std::string("cannot open pcap file");
        d->pcap.reset();
        return false;
    }
    return true;
}

void Processor::close()
{
    std::lock_guard<std::mutex> lock(d->mutex);
    if (d->pcap)
        d->pcap->close();
}

void Processor::reset()
{
    std::lock_guard<std::mutex> lock(d->mutex);
    d->streams.clear();
    d->startMs = steadyMs();
    d->lastSnapshotMs = 0;
}

void Processor::pushBbFrame(const uint8_t *frame, size_t len, const tbs::BbHeader &hdr)
{
    if (len < tbs::BbHeader::kSize || !hdr.valid())
        return;
    std::lock_guard<std::mutex> lock(d->mutex);
    Impl::Stream &s = d->stream(hdr.isi());
    ++s.frames;
    const uint8_t *df = frame + tbs::BbHeader::kSize;
    const size_t dfLen = std::min(len - tbs::BbHeader::kSize, hdr.dataFieldBytes());
    s.dataBytes += dfLen;
    s.header = hdr;
    s.hasHeader = true;

    switch (hdr.streamType()) {
    case tbs::BbHeader::StreamType::Transport: {
        s.sawTsType = true;
        if (!d->cfg.analyzeTables)
            break;
        s.tsBuf.clear();
        s.bb2ts.pushFrame(frame, len, hdr, [&s](const uint8_t *pkt) { s.tsBuf.insert(s.tsBuf.end(), pkt, pkt + 188); });
        if (!s.tsBuf.empty())
            s.psi.pushPackets(s.tsBuf.data(), s.tsBuf.size() / 188);
        break;
    }
    case tbs::BbHeader::StreamType::GenericContinuous:
    case tbs::BbHeader::StreamType::GseHem:
        if (d->cfg.decodeGse) {
            const bool hem = hdr.streamType() == tbs::BbHeader::StreamType::GseHem;
            s.gse.pushDataField(df, dfLen, hem, hdr.syncd, [this, &s](const GsePdu &pdu) { d->onPdu(s, pdu); });
        }
        break;
    case tbs::BbHeader::StreamType::GenericPacketized:
        s.sawGpType = true;
        break;
    }
}

void Processor::pushTs(const uint8_t *packets, size_t count)
{
    std::lock_guard<std::mutex> lock(d->mutex);
    Impl::Stream &s = d->stream(kTsInputKey);
    s.dataBytes += count * 188;
    if (d->cfg.analyzeTables)
        s.psi.pushPackets(packets, count);
}

ProcessorSnapshot Processor::snapshot()
{
    std::lock_guard<std::mutex> lock(d->mutex);
    const int64_t now = steadyMs();
    const double dt = d->lastSnapshotMs > 0 ? (now - d->lastSnapshotMs) / 1000.0 : 0.0;
    d->lastSnapshotMs = now;

    ProcessorSnapshot snap;
    snap.elapsedSec = (now - d->startMs) / 1000.0;
    for (auto &kv : d->streams) {
        Impl::Stream &s = *kv.second;
        StreamReport r;
        r.isi = s.key == kTsInputKey ? -1 : s.key;
        r.frames = s.frames;
        r.dataBytes = s.dataBytes;
        if (dt > 0)
            r.mbps = (s.dataBytes - s.dataBytesAtSnapshot) * 8.0 / dt / 1e6;
        s.dataBytesAtSnapshot = s.dataBytes;
        r.hasHeader = s.hasHeader;
        r.lastHeader = s.header;
        r.gse = s.gse.stats();
        r.tsPackets = s.psi.packets();

        if (s.key == kTsInputKey) {
            r.content = StreamContent::TsInput;
        } else if (s.sawTsType) {
            r.content = StreamContent::TransportStream;
        } else if (s.sawGpType) {
            r.content = StreamContent::GenericPacketized;
        } else if (r.gse.dataFields > 0) {
            // GSE if the large majority of data fields parse cleanly and PDUs come out
            const bool gse = r.gse.validDataFields * 10 >= r.gse.dataFields * 8 && (r.gse.packets > 0 || r.gse.paddingBytes > 0);
            r.content = gse ? StreamContent::Gse : StreamContent::GenericContinuous;
        }
        if (r.content == StreamContent::TransportStream || r.content == StreamContent::TsInput)
            r.ts = s.psi.report();
        if (r.content == StreamContent::Gse || r.gse.pdus > 0)
            r.ip = s.ip.report(d->cfg.topFlows, now);
        snap.streams.push_back(std::move(r));
    }
    if (d->pcap) {
        d->pcap->flush();
        snap.pcapOpen = d->pcap->isOpen();
        snap.pcapFile = d->pcap->fileName();
        snap.pcapPackets = d->pcap->packets();
        snap.pcapBytes = d->pcap->bytes();
    }
    snap.pcapError = d->pcapError;
    return snap;
}

} // namespace bb
} // namespace tbs
