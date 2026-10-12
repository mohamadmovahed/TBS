#include "StreamBuilders.h"

#include "tbsdvb/BbFrameExtractor.h"
#include "tbsdvb/BbFrameToTs.h"
#include "tbsdvb/BbHeader.h"
#include "tbsdvb/PlsUtil.h"
#include "tbsdvb/TsAnalyzer.h"
#include "tbsdvb/Types.h"

#include "tbsdvb/Receiver.h"
#include "tbsdvb/Sink.h"

#include <chrono>
#include <memory>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <thread>

using builders::Bytes;
using namespace tbs;

static int g_failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                              \
        }                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                                    \
    do {                                                                                                  \
        const auto va = (a);                                                                              \
        const auto vb = (b);                                                                              \
        if (!(va == vb)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK_EQ failed: %s (%lld) != %s (%lld)\n", __FILE__, __LINE__, #a, \
                         static_cast<long long>(va), #b, static_cast<long long>(vb));                     \
            ++g_failures;                                                                                 \
        }                                                                                                 \
    } while (0)

static void testCrc8()
{
    const char *check = "123456789";
    // CRC-8/DVB-S2 check value
    CHECK_EQ(BbHeader::crc8(reinterpret_cast<const uint8_t *>(check), 9), 0xBC);

    Bytes df(100, 0x55);
    Bytes nm = builders::makeBbFrame(0x52, 0x05, 0, df, 0xFFFF, 0, false);
    BbHeader h;
    CHECK(h.parse(nm.data()));
    CHECK(h.mode == BbHeader::Mode::Normal);
    CHECK(h.streamType() == BbHeader::StreamType::GenericContinuous);
    CHECK(!h.singleInputStream());
    CHECK_EQ(h.isi(), 5);
    CHECK_EQ(h.dfl, 800);

    Bytes hem = builders::makeBbFrame(0xB0, 0, 0, df, 0, 0, true);
    CHECK(h.parse(hem.data()));
    CHECK(h.mode == BbHeader::Mode::HighEfficiency);

    hem[3] ^= 0x10;
    CHECK(!h.parse(hem.data()));
}

static void testPls()
{
    CHECK_EQ(PlsUtil::goldToRoot(0), 1u);
    CHECK_EQ(PlsUtil::rootToGold(1), 0u);
    for (uint32_t g : {1u, 8u, 131070u, 262141u, 12345u})
        CHECK_EQ(PlsUtil::rootToGold(PlsUtil::goldToRoot(g)), g);
}

static void testTunerConfig()
{
    TuningParams c;
    c.lnbType = LnbType::Universal;
    c.frequencyMHz = 11494;
    CHECK_EQ(c.intermediateFrequencyKHz(), 1744000u);
    CHECK(!c.toneOn());
    c.frequencyMHz = 12000;
    CHECK_EQ(c.intermediateFrequencyKHz(), 1400000u);
    CHECK(c.toneOn());

    c.lnbType = LnbType::SingleLo;
    c.lnbLo1MHz = 5150;
    c.frequencyMHz = 3800;
    CHECK_EQ(c.intermediateFrequencyKHz(), 1350000u);
    CHECK(!c.toneOn());

    c.lnbType = LnbType::Direct;
    c.frequencyMHz = 1210.5;
    CHECK_EQ(c.intermediateFrequencyKHz(), 1210500u);

    c.inputStreamId = -1;
    CHECK_EQ(c.dvbStreamId(), ~0u);
    c.inputStreamId = 3;
    c.plsMode = PlsMode::Gold;
    c.plsCode = 8;
    CHECK_EQ(c.dvbStreamId(), (3u | (8u << 8) | (1u << 26)));
    c.plsMode = PlsMode::Root;
    c.plsCode = PlsUtil::goldToRoot(77);
    CHECK_EQ(c.goldCode(), 77u);
}

static void testTsAnalyzer()
{
    Bytes stream = {0x12, 0x47, 0x00, 0x99}; // garbage incl. a false sync byte
    for (int i = 0; i < 10; ++i) {
        Bytes p = builders::makeTsPacket(0x100, static_cast<uint8_t>(i), 0xAA);
        stream.insert(stream.end(), p.begin(), p.end());
    }
    TsAnalyzer ts;
    size_t got = 0;
    auto cb = [&](const uint8_t *pk, size_t n) {
        for (size_t i = 0; i < n; ++i)
            CHECK_EQ(pk[i * 188], 0x47);
        got += n;
    };
    // Feed in awkward chunk sizes
    for (size_t off = 0; off < stream.size(); off += 77)
        ts.push(stream.data() + off, std::min<size_t>(77, stream.size() - off), cb);
    CHECK_EQ(got, 10u);
    CHECK_EQ(ts.ccErrors(), 0u);
    CHECK_EQ(ts.pidCount(0x100), 10u);

    Bytes jump = builders::makeTsPacket(0x100, 14, 0xAA);
    Bytes after = builders::makeTsPacket(0x100, 15, 0xAA);
    ts.push(jump.data(), jump.size(), cb);
    ts.push(after.data(), after.size(), cb);
    CHECK_EQ(ts.ccErrors(), 1u);
    CHECK_EQ(got, 12u);
}

static std::vector<Bytes> makeProgramPackets(size_t n)
{
    std::vector<Bytes> pk;
    for (size_t i = 0; i < n; ++i) {
        Bytes p = builders::makeTsPacket(static_cast<uint16_t>(0x100 + (i % 3)), static_cast<uint8_t>(i / 3),
                                         static_cast<uint8_t>(i));
        for (size_t j = 4; j < 188; ++j)
            p[j] = static_cast<uint8_t>(i * 31 + j);
        pk.push_back(p);
    }
    return pk;
}

static void testRoundTrip(bool hem, size_t dfl)
{
    const auto packets = makeProgramPackets(600);
    const auto frames = builders::tsToBbFrames(packets, dfl, hem);

    Bytes pseudo;
    uint8_t cc = 0;
    for (const Bytes &f : frames)
        for (const Bytes &p : builders::bbFrameToPseudoTs(f, cc, 0x0118))
            pseudo.insert(pseudo.end(), p.begin(), p.end());

    TsAnalyzer ts;
    BbFrameExtractor bbx;
    BbFrameToTs b2t;
    std::vector<Bytes> recovered;
    size_t frameIdx = 0;
    bool framesMatch = true;

    auto onFrame = [&](const uint8_t *f, size_t len, const BbHeader &h) {
        if (frameIdx >= frames.size() || frames[frameIdx].size() != len
            || std::memcmp(frames[frameIdx].data(), f, len) != 0)
            framesMatch = false;
        ++frameIdx;
        b2t.pushFrame(f, len, h, [&](const uint8_t *p) { recovered.emplace_back(p, p + 188); });
    };
    for (size_t off = 0; off < pseudo.size(); off += 1000) {
        ts.push(pseudo.data() + off, std::min<size_t>(1000, pseudo.size() - off), [&](const uint8_t *pk, size_t n) {
            for (size_t i = 0; i < n; ++i)
                bbx.pushPacket(pk + i * 188, onFrame);
        });
    }

    CHECK(framesMatch);
    CHECK_EQ(bbx.frames(), frames.size());
    CHECK_EQ(bbx.crcErrors(), 0u);
    CHECK_EQ(bbx.sequenceErrors(), 0u);
    CHECK_EQ(bbx.lengthErrors(), 0u);
    CHECK_EQ(ts.ccErrors(), 0u);
    // The first frame starts with a packet boundary, so every packet but a
    // possible trailing partial one must come back unchanged.
    CHECK(recovered.size() + 1 >= packets.size());
    bool same = true;
    for (size_t i = 0; i < recovered.size(); ++i)
        same = same && recovered[i] == packets[i];
    CHECK(same);
}

static void testExtractorErrors()
{
    Bytes df(3000);
    for (size_t i = 0; i < df.size(); ++i)
        df[i] = static_cast<uint8_t>(i);
    const Bytes frame = builders::makeBbFrame(0x70, 0, 0, df, 0, 0);
    uint8_t cc = 0;
    auto pk1 = builders::bbFrameToPseudoTs(frame, cc);
    auto pk2 = builders::bbFrameToPseudoTs(frame, cc);
    auto pk3 = builders::bbFrameToPseudoTs(frame, cc);
    pk2.erase(pk2.begin() + 5); // lose a packet in the second frame

    BbFrameExtractor bbx;
    size_t ok = 0;
    auto cb = [&](const uint8_t *, size_t len, const BbHeader &) {
        CHECK_EQ(len, frame.size());
        ++ok;
    };
    for (auto *v : {&pk1, &pk2, &pk3})
        for (const Bytes &p : *v)
            bbx.pushPacket(p.data(), cb);
    CHECK_EQ(ok, 2u);
    CHECK_EQ(bbx.sequenceErrors(), 1u);

    // Frame that ends exactly on a packet boundary (no short last packet)
    BbFrameExtractor bbx2;
    Bytes df2(179 * 3 - 10);
    const Bytes exact = builders::makeBbFrame(0x70, 0, 0, df2, 0, 0);
    cc = 0;
    size_t n2 = 0;
    for (int rep = 0; rep < 3; ++rep)
        for (const Bytes &p : builders::bbFrameToPseudoTs(exact, cc))
            bbx2.pushPacket(p.data(), [&](const uint8_t *, size_t, const BbHeader &) { ++n2; });
    CHECK_EQ(n2, 3u);
}

// Library end to end: replay a pseudo-TS file through tbs::Receiver in BBFrame mode
// and check frames, recovered TS, the file sink and the stats.
static void testReceiverReplay()
{
    const auto packets = makeProgramPackets(3000);
    const auto frames = builders::tsToBbFrames(packets, 5000);
    const std::string in = "tbs_test_input.ts";
    const std::string out = "tbs_test_output.bbf";
    {
        std::FILE *f = std::fopen(in.c_str(), "wb");
        uint8_t cc = 0;
        for (const Bytes &fr : frames)
            for (const Bytes &p : builders::bbFrameToPseudoTs(fr, cc))
                std::fwrite(p.data(), 1, 188, f);
        std::fclose(f);
    }

    ReceiverConfig cfg;
    cfg.device.kind = DeviceKind::FileReplay;
    cfg.device.replayFile = in;
    cfg.device.replayLoop = false;
    cfg.capture = CaptureMode::BBFrame;
    cfg.file.enabled = true;
    cfg.file.path = out;
    cfg.file.timestampSuffix = false;
    cfg.file.bbFormat = BbFileFormat::Raw;

    Receiver rx;
    size_t gotFrames = 0;
    Bytes previewTs;
    std::string error = "none";
    Receiver::Callbacks cb;
    cb.onBbFrame = [&](const uint8_t *, size_t, const BbHeader &) { ++gotFrames; };
    cb.onPreviewTs = [&](const uint8_t *p, size_t n) { previewTs.insert(previewTs.end(), p, p + n); };
    cb.onStateChanged = [&](ReceiverState s, const std::string &e) {
        if (s == ReceiverState::Stopped)
            error = e;
    };
    rx.setCallbacks(cb);
    rx.setPreviewEnabled(true);
    rx.setStatusInterval(50);
    CHECK(rx.start(cfg));
    for (int i = 0; i < 200 && rx.isRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    rx.stop();

    CHECK(error.empty());
    CHECK_EQ(gotFrames, frames.size());
    CHECK_EQ(previewTs.size(), packets.size() * 188);
    bool same = previewTs.size() == packets.size() * 188;
    for (size_t i = 0; same && i < packets.size(); ++i)
        same = std::memcmp(previewTs.data() + i * 188, packets[i].data(), 188) == 0;
    CHECK(same);

    size_t expectedBytes = 0;
    for (const Bytes &fr : frames)
        expectedBytes += fr.size();
    std::FILE *f = std::fopen(out.c_str(), "rb");
    CHECK(f != nullptr);
    if (f) {
        std::fseek(f, 0, SEEK_END);
        CHECK_EQ(static_cast<size_t>(std::ftell(f)), expectedBytes);
        std::fclose(f);
    }
    std::remove(in.c_str());
    std::remove(out.c_str());

    // Opening a missing file reports an error through the state callback
    cfg.device.replayFile = "does_not_exist.ts";
    cfg.file.enabled = false;
    error.clear();
    CHECK(rx.start(cfg));
    for (int i = 0; i < 100 && rx.isRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    rx.stop();
    CHECK(!error.empty());
    CHECK(!rx.lastError().empty());
}

#if defined(TBS_TEST_LINUX_INTERNALS)
#include "platform/LinuxDvbDevice.h"

// Linux diagnostics against fake /sys, /proc, /dev and /lib trees
static void testLinuxDiagnostics()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::absolute("tbs_test_root");
    auto write = [&](const std::string &rel, const std::string &content) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel) << content;
    };
    auto has = [](const std::vector<DiagnosticItem> &items, DiagnosticItem::Level level, const std::string &text) {
        for (const auto &i : items)
            if (i.level == level && i.message.find(text) != std::string::npos)
                return true;
        return false;
    };
    DeviceSelection sel;

    // Card present, no driver bound, module installed but not loaded, no /dev/dvb
    fs::remove_all(root);
    write("proc/sys/kernel/osrelease", "6.1.0-test\n");
    write("proc/modules", "snd 1 0 - Live 0x0\n");
    const std::string pci = "sys/bus/pci/devices/0000:03:00.0/";
    write(pci + "vendor", "0x544d\n");
    write(pci + "device", "0x6178\n");
    write(pci + "subsystem_vendor", "0x6903\n");
    write(pci + "subsystem_device", "0x0021\n");
    write("lib/modules/6.1.0-test/updates/tbsecp3.ko.xz", "");
    auto items = LinuxDvbDevice::diagnose(sel, root.string());
    CHECK(has(items, DiagnosticItem::Level::Error, "TBS 6903x found at PCI 0000:03:00.0 but no driver"));
    CHECK(has(items, DiagnosticItem::Level::Warning, "tbsecp3 is installed"));
    CHECK(has(items, DiagnosticItem::Level::Error, "/dev/dvb has no adapters"));

    // Module missing for the running kernel
    fs::remove(root / "lib/modules/6.1.0-test/updates/tbsecp3.ko.xz");
    items = LinuxDvbDevice::diagnose(sel, root.string());
    CHECK(has(items, DiagnosticItem::Level::Error, "not installed for the running kernel 6.1.0-test"));

    // Everything in place; the user asks for a frontend that does not exist
    write("proc/modules", "stid135 1 0 - Live 0x0\ntbsecp3 1 0 - Live 0x0\ndvb_core 1 0 - Live 0x0\n");
    fs::create_directories(root / "sys/bus/pci/drivers/tbsecp3");
    fs::create_directory_symlink(root / "sys/bus/pci/drivers/tbsecp3", root / pci / "driver");
    write("dev/dvb/adapter0/frontend0", "");
    write("dev/dvb/adapter0/demux0", "");
    write("dev/dvb/adapter1/frontend0", "");
    write("sys/module/stid135/parameters/bbframe", "0\n");
    items = LinuxDvbDevice::diagnose(sel, root.string());
    CHECK(has(items, DiagnosticItem::Level::Ok, "TBS 6903x found at PCI 0000:03:00.0, driver: tbsecp3"));
    CHECK(has(items, DiagnosticItem::Level::Ok, "Module tbsecp3 loaded"));
    CHECK(has(items, DiagnosticItem::Level::Ok, "/dev/dvb/adapter0/frontend0 is accessible"));
    CHECK(has(items, DiagnosticItem::Level::Info, "stid135 bbframe=0"));
    sel.adapter = 2;
    items = LinuxDvbDevice::diagnose(sel, root.string());
    CHECK(has(items, DiagnosticItem::Level::Error, "/dev/dvb/adapter2/frontend0 does not exist"));
    CHECK(!Devices::formatDiagnostics(items).empty());
    fs::remove_all(root);
}
#endif

// A sink that is too slow: the capture thread must not stall, data is dropped and counted
class SlowSink : public IOutputSink
{
public:
    bool open(std::string *) override { return true; }
    void close() override {}
    bool acceptsTs(CaptureMode) const override { return true; }
    bool acceptsBbFrames(CaptureMode) const override { return false; }
    void writeTs(const uint8_t *, size_t count) override
    {
        packets += count;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    void writeBbFrame(const uint8_t *, size_t, const BbHeader &) override {}
    size_t packets = 0;
};

static void testPipelineBackpressure()
{
    const std::string in = "tbs_test_pipe.ts";
    {
        std::FILE *f = std::fopen(in.c_str(), "wb");
        for (int i = 0; i < 60000; ++i) { // ~11 MB
            Bytes p = builders::makeTsPacket(0x100, static_cast<uint8_t>(i), 0x11);
            std::fwrite(p.data(), 1, 188, f);
        }
        std::fclose(f);
    }
    ReceiverConfig cfg;
    cfg.device.kind = DeviceKind::FileReplay;
    cfg.device.replayFile = in;
    cfg.device.replayLoop = false;

    Receiver rx;
    auto sink = std::make_shared<SlowSink>();
    rx.addSink(sink);
    rx.setPipelineBufferSize(1u << 20); // tiny buffer
    rx.setStatusInterval(50);
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(rx.start(cfg));
    for (int i = 0; i < 500 && rx.isRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    rx.stop();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const StreamStats st = rx.stats();
    CHECK_EQ(st.bytesIn, 60000u * 188);        // the reader consumed everything
    CHECK(st.pipelineDroppedBytes > 0);          // ... and dropped what processing could not take
    CHECK(st.pipelineCapacityBytes == (1u << 20));
    CHECK_EQ(st.tsPackets * 188 + st.pipelineDroppedBytes, st.bytesIn);
    CHECK_EQ(sink->packets, st.tsPackets);
    CHECK(secs < 5.0);
    std::remove(in.c_str());
}

static void testFileSegments()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::absolute("tbs_test_rec");
    fs::remove_all(dir);
    FileOutputOptions opt;
    opt.enabled = true;
    opt.path = (dir / "cap.ts").string();
    opt.timestampSuffix = false;
    opt.splitSizeMB = 1;
    opt.keepFiles = 2;
    opt.minFreeMB = 1;
    FileSink sink(opt, CaptureMode::TransportStream);
    std::string err;
    CHECK(sink.open(&err));
    std::vector<uint8_t> chunk(188 * 1000, 0x47); // 188000 bytes per write
    // A segment is closed once it holds >= 1 MiB: 6 writes (1,128,000 bytes) -> 30 writes = 5 segments
    for (int i = 0; i < 30; ++i)
        sink.writeTs(chunk.data(), 1000);
    sink.close();
    CHECK_EQ(sink.segments(), 5u);
    CHECK_EQ(sink.bytesWritten(), 30u * 188000);
    CHECK(!fs::exists(dir / "cap_001.ts")); // rolled out (keep 2)
    CHECK(!fs::exists(dir / "cap_003.ts"));
    CHECK(fs::exists(dir / "cap_004.ts"));
    CHECK(fs::exists(dir / "cap_005.ts"));
    CHECK_EQ(fs::file_size(dir / "cap_004.ts"), 6u * 188000);
    StreamStats st;
    sink.fillStats(st);
    CHECK(st.fileName.find("cap_005.ts") != std::string::npos);
    CHECK(st.fileFreeBytes > 0);
    CHECK(!st.fileStopped);

    // Not enough free space: refuse to start
    FileOutputOptions full = opt;
    full.path = (dir / "full.ts").string();
    full.minFreeMB = 1ull << 40; // 1 EiB
    FileSink fullSink(full, CaptureMode::TransportStream);
    CHECK(!fullSink.open(&err));
    CHECK(err.find("free disk space") != std::string::npos);
    fs::remove_all(dir);
}

int main()
{
    testCrc8();
    testPls();
    testTunerConfig();
    testTsAnalyzer();
    testRoundTrip(false, 7264);
    testRoundTrip(false, 1000);
    testRoundTrip(true, 7264);
    testRoundTrip(true, 333);
    testExtractorErrors();
    testReceiverReplay();
    testPipelineBackpressure();
    testFileSegments();
#if defined(TBS_TEST_LINUX_INTERNALS)
    testLinuxDiagnostics();
#endif

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All tests passed\n");
    return 0;
}
