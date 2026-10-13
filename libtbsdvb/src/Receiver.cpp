#include "tbsdvb/Receiver.h"

#include "ChunkQueue.h"
#include "Util.h"
#include "tbsdvb/BbFrameExtractor.h"
#include "tbsdvb/BbFrameToTs.h"
#include "tbsdvb/DvbNames.h"
#include "tbsdvb/TsAnalyzer.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace tbs {

struct Receiver::Impl
{
    Callbacks cb;
    std::vector<std::shared_ptr<IOutputSink>> extraSinks;
    int statusIntervalMs = 500;
    size_t pipelineBytes = 128u << 20;

    std::thread thread;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> preview{false};
    std::atomic<int> previewIsi{-1};
    std::atomic<ReceiverState> state{ReceiverState::Stopped};

    mutable std::mutex mutex; // guards the members below
    ReceiverConfig config;
    FrontendStatus status;
    StreamStats stats;
    std::string lastError;
    std::optional<TuningParams> pendingTune;

    void log(const std::string &m)
    {
        if (cb.onLog)
            cb.onLog(m);
    }
    void setState(ReceiverState s, const std::string &error = {})
    {
        state = s;
        if (!error.empty()) {
            std::lock_guard<std::mutex> lock(mutex);
            lastError = error;
        }
        if (cb.onStateChanged)
            cb.onStateChanged(s, error);
    }
    void run(ReceiverConfig cfg);
};

Receiver::Receiver()
    : d(std::make_unique<Impl>())
{
}

Receiver::~Receiver()
{
    stop();
}

void Receiver::setCallbacks(Callbacks cb)
{
    d->cb = std::move(cb);
}

void Receiver::addSink(std::shared_ptr<IOutputSink> sink)
{
    if (sink)
        d->extraSinks.push_back(std::move(sink));
}

void Receiver::clearSinks()
{
    d->extraSinks.clear();
}

void Receiver::setPipelineBufferSize(size_t bytes)
{
    d->pipelineBytes = bytes;
}

void Receiver::setStatusInterval(int ms)
{
    d->statusIntervalMs = ms < 50 ? 50 : ms;
}

bool Receiver::start(const ReceiverConfig &cfg)
{
    if (isRunning())
        return false;
    if (d->thread.joinable())
        d->thread.join();
    {
        std::lock_guard<std::mutex> lock(d->mutex);
        d->config = cfg;
        d->status = FrontendStatus();
        d->stats = StreamStats();
        d->lastError.clear();
        d->pendingTune.reset();
    }
    d->stopRequested = false;
    d->state = ReceiverState::Starting;
    d->thread = std::thread(&Impl::run, d.get(), cfg);
    return true;
}

void Receiver::stop()
{
    d->stopRequested = true;
    if (d->thread.joinable())
        d->thread.join();
}

bool Receiver::isRunning() const
{
    return d->state.load() != ReceiverState::Stopped;
}

ReceiverState Receiver::state() const
{
    return d->state.load();
}

std::string Receiver::lastError() const
{
    std::lock_guard<std::mutex> lock(d->mutex);
    return d->lastError;
}

void Receiver::retune(const TuningParams &tuning)
{
    std::lock_guard<std::mutex> lock(d->mutex);
    d->pendingTune = tuning;
}

void Receiver::setPreviewStream(int isi)
{
    d->previewIsi = isi;
}

void Receiver::setPreviewEnabled(bool on)
{
    d->preview = on;
}

FrontendStatus Receiver::status() const
{
    std::lock_guard<std::mutex> lock(d->mutex);
    return d->status;
}

StreamStats Receiver::stats() const
{
    std::lock_guard<std::mutex> lock(d->mutex);
    return d->stats;
}

ReceiverConfig Receiver::config() const
{
    std::lock_guard<std::mutex> lock(d->mutex);
    return d->config;
}

void Receiver::Impl::run(ReceiverConfig cfg)
{
    std::string err;
    std::unique_ptr<ITunerDevice> device;
    std::vector<std::shared_ptr<IOutputSink>> sinks;
    std::shared_ptr<FileSink> fileSink;
    std::shared_ptr<ZmqSink> zmqSink;
    std::vector<std::shared_ptr<TsNetworkSink>> netSinks;
    const bool bbMode = cfg.capture == CaptureMode::BBFrame;

    auto finish = [&](const std::string &error) {
        setState(ReceiverState::Stopping);
        if (!error.empty())
            log("ERROR: " + error);
        for (auto &s : sinks)
            s->close();
        sinks.clear();
        if (device)
            device->close();
        device.reset();
        setState(ReceiverState::Stopped, error);
    };

    setState(ReceiverState::Starting);
    log(std::string("Starting ") + (bbMode ? "BBFrame" : "TS") + " capture (tbsdvb " + version() + ")");

    device = Devices::create(cfg.device.kind, &err);
    if (!device)
        return finish(err);
    device->setLogger([this](const std::string &m) { log(m); });

    if (bbMode && cfg.device.kind == DeviceKind::LinuxDvb) {
        const int p = Devices::stid135BbFrameParameter();
        if (p == 0)
            log("WARNING: stid135 module parameter bbframe=0, the card will output plain TS. Add 'options stid135 "
                "bbframe=1' to /etc/modprobe.d/stid135.conf and reload the driver.");
        else if (p < 0)
            log("Note: /sys/module/stid135/parameters/bbframe not found (driver not loaded or not the TBS stid135 driver)");
        else
            log(util::format("stid135 bbframe=%d", p));
    }

    if (!device->open(cfg.device, cfg.tuning, cfg.capture, &err))
        return finish(err);

    if (cfg.file.enabled) {
        fileSink = std::make_shared<FileSink>(cfg.file, cfg.capture);
        if (!fileSink->open(&err))
            return finish(err);
        log("Recording to " + fileSink->fileName());
        sinks.push_back(fileSink);
    }
    if (cfg.zmq.enabled) {
        zmqSink = std::make_shared<ZmqSink>(cfg.zmq, cfg.capture);
        if (!zmqSink->open(&err))
            return finish(err);
        log(std::string("ZeroMQ ") + (cfg.zmq.socket == ZmqSocketKind::Pub ? "PUB " : "PUSH ")
            + (cfg.zmq.bind ? "bound to " : "connected to ") + cfg.zmq.endpoint);
        sinks.push_back(zmqSink);
    }
    for (const TsNetworkOutput &o : cfg.network) {
        if (!o.enabled)
            continue;
        auto ns = std::make_shared<TsNetworkSink>(o, cfg.capture);
        if (!ns->open(&err))
            return finish(err);
        std::string what = o.programNumber > 0 ? util::format("service %d", o.programNumber)
                           : o.pids.empty()    ? std::string("the transport stream")
                                               : util::format("%u PIDs", static_cast<unsigned>(o.pids.size()));
        if (bbMode)
            what += o.isi >= 0 ? util::format(" of ISI %d", o.isi) : std::string(" of the first TS input stream");
        log("Streaming " + what + " to " + networkUrl(o, true));
        netSinks.push_back(ns);
        sinks.push_back(ns);
    }
    for (auto &s : extraSinks) {
        if (!s->open(&err))
            return finish(err);
        sinks.push_back(s);
    }

    if (!device->startStream(&err))
        return finish(err);
    setState(ReceiverState::Running);

    std::vector<IOutputSink *> tsSinks;
    std::vector<IOutputSink *> bbSinks;
    for (auto &s : sinks) {
        if (s->acceptsTs(cfg.capture))
            tsSinks.push_back(s.get());
        if (s->acceptsBbFrames(cfg.capture))
            bbSinks.push_back(s.get());
    }

    ChunkQueue queue(pipelineBytes);
    std::atomic<uint64_t> bytesIn{0};
    std::atomic<uint64_t> overflows{0};
    const int64_t t0 = util::nowMs();

    // ---------------------------------------------------------------------
    // Processing thread: parsing, sinks, callbacks and the stream statistics
    // ---------------------------------------------------------------------
    uint64_t finalPackets = 0;
    uint64_t finalFrames = 0;
    std::thread processing([&]() {
        TsAnalyzer ts;
        BbFrameExtractor bbx;
        BbFrameToTs bb2ts;
        BbHeader lastHeader;
        bool haveHeader = false;
        bool previewOn = false;
        int previewWant = -1;   // requested ISI
        int previewLocked = -2; // ISI in use (-2: none yet)
        std::vector<uint8_t> previewBuf;
        previewBuf.reserve(188 * 512);

        auto previewPacket = [&](const uint8_t *pkt) { previewBuf.insert(previewBuf.end(), pkt, pkt + 188); };
        auto onFrame = [&](const uint8_t *frame, size_t len, const BbHeader &hdr) {
            lastHeader = hdr;
            haveHeader = true;
            for (IOutputSink *s : bbSinks)
                s->writeBbFrame(frame, len, hdr);
            if (cb.onBbFrame)
                cb.onBbFrame(frame, len, hdr);
            if (previewOn && hdr.streamType() == BbHeader::StreamType::Transport) {
                // One input stream only: TS of several ISIs must not be mixed
                const int want = previewIsi.load();
                if (want != previewWant) {
                    previewWant = want;
                    previewLocked = -2;
                    bb2ts.reset();
                }
                const int isi = hdr.isi();
                if (isi >= 0) {
                    if (want >= 0 && isi != want)
                        return;
                    if (previewLocked == -2)
                        previewLocked = isi;
                    else if (isi != previewLocked)
                        return;
                }
                bb2ts.pushFrame(frame, len, hdr, previewPacket);
            }
        };
        auto onPackets = [&](const uint8_t *pk, size_t count) {
            for (IOutputSink *s : tsSinks)
                s->writeTs(pk, count);
            if (cb.onTsPackets)
                cb.onTsPackets(pk, count);
            if (!bbMode) {
                if (previewOn && cb.onPreviewTs)
                    cb.onPreviewTs(pk, count * 188);
                return;
            }
            for (size_t i = 0; i < count; ++i)
                bbx.pushPacket(pk + i * 188, onFrame);
        };

        int64_t lastReport = t0;
        uint64_t lastBytes = 0;
        uint64_t lastFrames = 0;
        uint64_t lastPayload = 0;
        uint64_t lastDropped = 0;
        std::vector<uint64_t> lastNetBytes(netSinks.size(), 0);
        bool warnedNoBb = false;
        bool fileErrorReported = false;

        auto report = [&](int64_t now) {
            const double dt = std::max<int64_t>(now - lastReport, 1) / 1000.0;
            lastReport = now;
            StreamStats st;
            st.elapsedSec = (now - t0) / 1000.0;
            st.bytesIn = bytesIn.load();
            st.inputMbps = (st.bytesIn - lastBytes) * 8.0 / dt / 1e6;
            lastBytes = st.bytesIn;
            st.driverOverflows = overflows.load();
            const ChunkQueue::Stats q = queue.stats();
            st.pipelineCapacityBytes = q.capacity;
            st.pipelineQueuedBytes = q.queued;
            st.pipelinePeakBytes = q.peak;
            st.pipelineDroppedBytes = q.droppedBytes;
            if (q.droppedBytes > lastDropped) {
                log(util::format("WARNING: processing cannot keep up, %llu bytes dropped (buffer %llu MiB). Check the "
                                 "disk / network outputs.",
                                 static_cast<unsigned long long>(q.droppedBytes - lastDropped),
                                 static_cast<unsigned long long>(q.capacity >> 20)));
                lastDropped = q.droppedBytes;
            }
            st.tsPackets = ts.packets();
            st.syncLosses = ts.syncLosses();
            st.ccErrors = ts.ccErrors();
            st.teiErrors = ts.teiErrors();
            st.pidCounts = ts.pidSnapshot();
            st.bbFrames = bbx.frames();
            st.bbFramesPerSec = (bbx.frames() - lastFrames) / dt;
            st.bbPayloadMbps = (bbx.payloadBytes() - lastPayload) * 8.0 / dt / 1e6;
            lastFrames = bbx.frames();
            lastPayload = bbx.payloadBytes();
            st.bbCrcErrors = bbx.crcErrors();
            st.bbSequenceErrors = bbx.sequenceErrors();
            st.bbLengthErrors = bbx.lengthErrors();
            st.hasBbHeader = haveHeader;
            st.lastBbHeader = lastHeader;
            if (fileSink) {
                fileSink->fillStats(st);
                if (!fileSink->lastError().empty() && !fileErrorReported) {
                    fileErrorReported = true;
                    log("ERROR: recording stopped: " + fileSink->lastError());
                }
            }
            if (zmqSink) {
                st.zmqMessages = zmqSink->messages();
                st.zmqDropped = zmqSink->dropped();
            }
            for (size_t i = 0; i < netSinks.size(); ++i) {
                NetOutputStatus ns = netSinks[i]->status();
                ns.mbps = (ns.bytes - lastNetBytes[i]) * 8.0 / dt / 1e6;
                lastNetBytes[i] = ns.bytes;
                st.network.push_back(std::move(ns));
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                stats = st;
            }
            if (cb.onStats)
                cb.onStats(st);

            if (bbMode && !warnedNoBb && st.elapsedSec > 3.0 && ts.packets() > 1000 && bbx.carrierPackets() == 0) {
                warnedNoBb = true;
                log("WARNING: TS is arriving but no BBFrame packets (PID 0x010E/0x0118). The driver only outputs "
                    "BBFrames in BBFrame mode (Linux: stid135 bbframe=1) and for Generic Stream/GSE carriers.");
            }
        };

        while (true) {
            bool finished = false;
            std::unique_ptr<ChunkQueue::Chunk> c = queue.pop(50, &finished);
            if (c) {
                if (c->reset) {
                    ts.reset();
                    bbx.reset();
                    bb2ts.reset();
                } else {
                    previewOn = preview.load() && static_cast<bool>(cb.onPreviewTs);
                    ts.push(c->data.data(), c->len, onPackets);
                    if (!previewBuf.empty()) {
                        if (previewOn)
                            cb.onPreviewTs(previewBuf.data(), previewBuf.size());
                        previewBuf.clear();
                    }
                }
                queue.recycle(std::move(c));
            } else if (finished) {
                break;
            }
            const int64_t now = util::nowMs();
            if (now - lastReport >= statusIntervalMs)
                report(now);
        }
        report(util::nowMs());
        finalPackets = ts.packets();
        finalFrames = bbx.frames();
    });

    // ---------------------------------------------------------------------
    // Capture thread (this one): device reads, retunes, frontend status
    // ---------------------------------------------------------------------
    constexpr size_t kReadSize = 188 * 2048;
    int64_t lastStatus = t0;
    bool wasLocked = false;
    std::string error;

    auto status_ = [&]() {
        FrontendStatus fe = device->readStatus();
        if (fe.locked() != wasLocked) {
            wasLocked = fe.locked();
            log(wasLocked ? std::string("Frontend LOCKED") : "Frontend lost lock (" + DvbNames::statusFlags(fe.flags) + ")");
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            status = fe;
        }
        if (cb.onStatus)
            cb.onStatus(fe);
    };

    while (!stopRequested.load()) {
        std::optional<TuningParams> tune;
        {
            std::lock_guard<std::mutex> lock(mutex);
            tune.swap(pendingTune);
        }
        if (tune) {
            std::string tuneErr;
            if (device->tune(*tune, &tuneErr)) {
                std::lock_guard<std::mutex> lock(mutex);
                config.tuning = *tune;
            } else {
                log("ERROR: retune failed: " + tuneErr);
            }
            auto marker = queue.acquire(0);
            marker->reset = true;
            queue.push(std::move(marker), true);
        }

        auto chunk = queue.acquire(kReadSize);
        const long n = device->read(chunk->data.data(), kReadSize, 100);
        overflows = device->overflows();
        if (n < 0) {
            queue.recycle(std::move(chunk));
            error = "Read error from device";
            break;
        }
        if (n > 0) {
            chunk->len = static_cast<size_t>(n);
            bytesIn += static_cast<uint64_t>(n);
            queue.push(std::move(chunk));
        } else {
            queue.recycle(std::move(chunk));
        }

        const int64_t now = util::nowMs();
        if (now - lastStatus >= statusIntervalMs) {
            lastStatus = now;
            status_();
        }
        if (device->atEnd())
            break;
    }

    // Let the processing thread drain what is buffered, then shut down
    queue.close();
    processing.join();
    status_();
    log(util::format("Capture stopped: %llu bytes, %llu TS packets, %llu BBFrames",
                     static_cast<unsigned long long>(bytesIn.load()), static_cast<unsigned long long>(finalPackets),
                     static_cast<unsigned long long>(finalFrames)));
    finish(error);
}

} // namespace tbs
