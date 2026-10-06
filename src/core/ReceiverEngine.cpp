#include "ReceiverEngine.h"

#include "BbFrameExtractor.h"
#include "BbFrameToTs.h"
#include "DeviceFactory.h"
#include "DvbNames.h"
#include "ITsConsumer.h"
#include "TsAnalyzer.h"
#include "output/FileSink.h"
#include "output/ZmqSink.h"

#include <QElapsedTimer>
#include <memory>
#include <vector>

ReceiverEngine::ReceiverEngine(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<FrontendStatus>("FrontendStatus");
    qRegisterMetaType<StreamStats>("StreamStats");
}

ReceiverEngine::~ReceiverEngine()
{
    stop();
}

bool ReceiverEngine::start(const TunerConfig &cfg)
{
    if (m_running.load())
        return false;
    if (m_thread.joinable())
        m_thread.join();
    m_stopRequested = false;
    m_running = true;
    m_thread = std::thread(&ReceiverEngine::run, this, cfg);
    return true;
}

void ReceiverEngine::stop()
{
    m_stopRequested = true;
    if (m_thread.joinable())
        m_thread.join();
}

void ReceiverEngine::run(TunerConfig cfg)
{
    QString err;
    std::unique_ptr<ITunerDevice> device;
    std::vector<std::unique_ptr<IOutputSink>> sinks;
    FileSink *fileSink = nullptr;
    ZmqSink *zmqSink = nullptr;

    auto fail = [&](const QString &msg) {
        emit logMessage(QStringLiteral("ERROR: ") + msg);
        emit failed(msg);
    };
    auto cleanup = [&]() {
        for (auto &s : sinks)
            s->close();
        sinks.clear();
        if (device)
            device->close();
        m_running = false;
        emit stopped();
    };

    const bool bbMode = cfg.captureMode == CaptureMode::BBFrame;
    emit logMessage(QStringLiteral("Starting %1 capture").arg(bbMode ? QStringLiteral("BBFrame") : QStringLiteral("TS")));

    device = DeviceFactory::create(cfg, &err);
    if (!device) {
        fail(err);
        cleanup();
        return;
    }
    device->setLogger([this](const QString &m) { emit logMessage(m); });

    if (bbMode && cfg.deviceKind == DeviceKind::LinuxDvb) {
        const int p = DeviceFactory::stid135BbFrameParameter();
        if (p == 0)
            emit logMessage(QStringLiteral("WARNING: stid135 module parameter bbframe=0. The card will output plain TS. "
                                           "Add 'options stid135 bbframe=1' to /etc/modprobe.d/stid135.conf and reload the driver."));
        else if (p < 0)
            emit logMessage(QStringLiteral("Note: /sys/module/stid135/parameters/bbframe not found (driver not loaded or not the TBS stid135 driver)"));
        else
            emit logMessage(QStringLiteral("stid135 bbframe=%1").arg(p));
    }

    if (!device->open(cfg, &err)) {
        fail(err);
        cleanup();
        return;
    }

    if (cfg.fileEnabled) {
        auto s = std::make_unique<FileSink>(cfg);
        if (!s->open(&err)) {
            fail(err);
            cleanup();
            return;
        }
        emit logMessage(QStringLiteral("Recording to %1").arg(s->fileName()));
        fileSink = s.get();
        sinks.push_back(std::move(s));
    }
    if (cfg.zmqEnabled) {
        auto s = std::make_unique<ZmqSink>(cfg);
        if (!s->open(&err)) {
            fail(err);
            cleanup();
            return;
        }
        emit logMessage(QStringLiteral("ZeroMQ %1 %2 %3")
                            .arg(cfg.zmqSocket == ZmqSocketKind::Pub ? QStringLiteral("PUB") : QStringLiteral("PUSH"),
                                 cfg.zmqBind ? QStringLiteral("bound to") : QStringLiteral("connected to"), cfg.zmqEndpoint));
        zmqSink = s.get();
        sinks.push_back(std::move(s));
    }

    if (!device->startStream(&err)) {
        fail(err);
        cleanup();
        return;
    }
    emit started();

    std::vector<IOutputSink *> tsSinks;
    std::vector<IOutputSink *> bbSinks;
    for (auto &s : sinks) {
        if (s->acceptsTs())
            tsSinks.push_back(s.get());
        if (s->acceptsBbFrames())
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
    ITsConsumer *preview = nullptr;

    auto previewPacket = [&](const uint8_t *pkt) { previewBuf.insert(previewBuf.end(), pkt, pkt + 188); };

    auto onFrame = [&](const uint8_t *frame, size_t len, const BbHeader &hdr) {
        lastHeader = hdr;
        haveHeader = true;
        for (IOutputSink *s : bbSinks)
            s->writeBbFrame(frame, len, hdr);
        if (preview)
            bb2ts.pushFrame(frame, len, hdr, previewPacket);
    };

    auto onPackets = [&](const uint8_t *pk, size_t count) {
        for (IOutputSink *s : tsSinks)
            s->writeTs(pk, count);
        if (!bbMode) {
            if (preview)
                preview->consumeTs(pk, count * 188);
            return;
        }
        for (size_t i = 0; i < count; ++i)
            bbx.pushPacket(pk + i * 188, onFrame);
    };

    QElapsedTimer clock;
    clock.start();
    qint64 lastReport = 0;
    uint64_t bytesIn = 0;
    uint64_t lastBytes = 0;
    uint64_t lastFrames = 0;
    uint64_t lastPayload = 0;
    bool wasLocked = false;
    bool warnedNoBb = false;
    bool fileErrorReported = false;

    while (!m_stopRequested.load()) {
        const long n = device->read(buf.data(), buf.size(), 100);
        if (n < 0) {
            fail(QStringLiteral("Read error from device"));
            break;
        }
        if (n > 0) {
            bytesIn += static_cast<uint64_t>(n);
            preview = m_preview.load();
            ts.push(buf.data(), static_cast<size_t>(n), onPackets);
            if (preview && !previewBuf.empty()) {
                preview->consumeTs(previewBuf.data(), previewBuf.size());
                previewBuf.clear();
            }
        }

        const qint64 now = clock.elapsed();
        if (now - lastReport >= 500) {
            const double dt = (now - lastReport) / 1000.0;
            lastReport = now;

            FrontendStatus fe = device->readStatus();
            if (fe.locked() != wasLocked) {
                wasLocked = fe.locked();
                emit logMessage(wasLocked ? QStringLiteral("Frontend LOCKED")
                                          : QStringLiteral("Frontend lost lock (%1)").arg(DvbNames::statusFlags(fe.flags)));
            }
            emit frontendStatusChanged(fe);

            StreamStats st;
            st.elapsedSec = now / 1000.0;
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
                if (!fileSink->lastError().isEmpty() && !fileErrorReported) {
                    fileErrorReported = true;
                    emit logMessage(QStringLiteral("ERROR: file write failed: ") + fileSink->lastError());
                }
            }
            if (zmqSink) {
                st.zmqMessages = zmqSink->messages();
                st.zmqDropped = zmqSink->dropped();
            }
            emit streamStatsChanged(st);

            if (bbMode && !warnedNoBb && now > 3000 && ts.packets() > 1000 && bbx.carrierPackets() == 0) {
                warnedNoBb = true;
                emit logMessage(QStringLiteral("WARNING: TS is arriving but no BBFrame packets (PID 0x010E/0x0118). "
                                               "The STiD135 driver only outputs BBFrames when loaded with bbframe=1 and "
                                               "the carrier is a Generic Stream / GSE (TS carriers stay TS)."));
            }
        }

        if (device->atEnd())
            break;
    }

    emit logMessage(QStringLiteral("Capture stopped: %1 bytes, %2 TS packets, %3 BBFrames")
                        .arg(bytesIn)
                        .arg(ts.packets())
                        .arg(bbx.frames()));
    cleanup();
}
