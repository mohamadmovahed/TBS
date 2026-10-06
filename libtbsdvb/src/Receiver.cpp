#include "tbsdvb/Receiver.h"

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

    std::thread thread;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> preview{false};
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

    std::vector<uint8_t> buf(188 * 2048);
    std::vector<uint8_t> previewBuf;
    previewBuf.reserve(188 * 512);
    TsAnalyzer ts;
    BbFrameExtractor bbx;
    BbFrameToTs bb2ts;
    BbHeader lastHeader;
    bool haveHeader = false;
    bool previewOn = false;

    auto previewPacket = [&](const uint8_t *pkt) { previewBuf.insert(previewBuf.end(), pkt, pkt + 188); };

    auto onFrame = [&](const uint8_t *frame, size_t len, const BbHeader &hdr) {
        lastHeader = hdr;
        haveHeader = true;
        for (IOutputSink *s : bbSinks)
            s->writeBbFrame(frame, len, hdr);
        if (cb.onBbFrame)
            cb.onBbFrame(frame, len, hdr);
        if (previewOn)
            bb2ts.pushFrame(frame, len, hdr, previewPacket);
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

    const int64_t t0 = util::nowMs();
    int64_t lastReport = t0;
    uint64_t bytesIn = 0;
    uint64_t lastBytes = 0;
    uint64_t lastFrames = 0;
    uint64_t lastPayload = 0;
    bool wasLocked = false;
    bool warnedNoBb = false;
    bool fileErrorReported = false;
    std::string error;

    auto report = [&](int64_t now) {
        const double dt = std::max<int64_t>(now - lastReport, 1) / 1000.0;
        lastReport = now;

        FrontendStatus fe = device->readStatus();
        if (fe.locked() != wasLocked) {
            wasLocked = fe.locked();
            log(wasLocked ? std::string("Frontend LOCKED") : "Frontend lost lock (" + DvbNames::statusFlags(fe.flags) + ")");
        }

        StreamStats st;
        st.elapsedSec = (now - t0) / 1000.0;
        st.bytesIn = bytesIn;
        st.inputMbps = (bytesIn - lastBytes) * 8.0 / dt / 1e6;
        lastBytes = bytesIn;
        st.driverOverflows = device->overflows();
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
            st.fileBytes = fileSink->bytesWritten();
            st.fileName = fileSink->fileName();
            if (!fileSink->lastError().empty() && !fileErrorReported) {
                fileErrorReported = true;
                log("ERROR: file write failed: " + fileSink->lastError());
            }
        }
        if (zmqSink) {
            st.zmqMessages = zmqSink->messages();
            st.zmqDropped = zmqSink->dropped();
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            status = fe;
            stats = st;
        }
        if (cb.onStatus)
            cb.onStatus(fe);
        if (cb.onStats)
            cb.onStats(st);

        if (bbMode && !warnedNoBb && st.elapsedSec > 3.0 && ts.packets() > 1000 && bbx.carrierPackets() == 0) {
            warnedNoBb = true;
            log("WARNING: TS is arriving but no BBFrame packets (PID 0x010E/0x0118). The driver only outputs "
                "BBFrames in BBFrame mode (Linux: stid135 bbframe=1) and for Generic Stream/GSE carriers.");
        }
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
            ts.reset();
            bbx.reset();
            bb2ts.reset();
        }

        const long n = device->read(buf.data(), buf.size(), 100);
        if (n < 0) {
            error = "Read error from device";
            break;
        }
        if (n > 0) {
            bytesIn += static_cast<uint64_t>(n);
            previewOn = preview.load() && static_cast<bool>(cb.onPreviewTs);
            ts.push(buf.data(), static_cast<size_t>(n), onPackets);
            if (!previewBuf.empty()) {
                if (previewOn)
                    cb.onPreviewTs(previewBuf.data(), previewBuf.size());
                previewBuf.clear();
            }
        }

        const int64_t now = util::nowMs();
        if (now - lastReport >= statusIntervalMs)
            report(now);

        if (device->atEnd())
            break;
    }

    report(util::nowMs());
    log(util::format("Capture stopped: %llu bytes, %llu TS packets, %llu BBFrames", static_cast<unsigned long long>(bytesIn),
                     static_cast<unsigned long long>(ts.packets()), static_cast<unsigned long long>(bbx.frames())));
    finish(error);
}

} // namespace tbs
