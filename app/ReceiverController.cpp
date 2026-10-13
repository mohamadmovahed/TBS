#include "ReceiverController.h"

ReceiverController::ReceiverController(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<tbs::FrontendStatus>("tbs::FrontendStatus");
    qRegisterMetaType<tbs::StreamStats>("tbs::StreamStats");
}

ReceiverController::~ReceiverController()
{
    m_rx.stop();
}

void ReceiverController::setPreviewSink(std::function<void(const uint8_t *, size_t)> sink)
{
    m_previewSink = std::move(sink);
}

void ReceiverController::setPreviewStream(int isi)
{
    m_rx.setPreviewStream(isi);
}

void ReceiverController::setPreviewEnabled(bool on)
{
    m_rx.setPreviewEnabled(on);
}

bool ReceiverController::start(const tbs::ReceiverConfig &cfg)
{
    tbs::Receiver::Callbacks cb;
    // Signals emitted from the worker thread are queued to receivers living in the GUI thread
    cb.onLog = [this](const std::string &m) { emit logMessage(QString::fromStdString(m)); };
    cb.onStateChanged = [this](tbs::ReceiverState s, const std::string &e) {
        emit stateChanged(static_cast<int>(s), QString::fromStdString(e));
    };
    cb.onStatus = [this](const tbs::FrontendStatus &s) { emit statusUpdated(s); };
    cb.onStats = [this](const tbs::StreamStats &s) { emit statsUpdated(s); };
    if (m_previewSink)
        cb.onPreviewTs = m_previewSink;
    if (auto proc = m_processor) {
        // Runs on the receiver thread; the processor is internally locked
        if (cfg.capture == tbs::CaptureMode::BBFrame)
            cb.onBbFrame = [proc](const uint8_t *f, size_t n, const tbs::BbHeader &h) { proc->pushBbFrame(f, n, h); };
        else
            cb.onTsPackets = [proc](const uint8_t *p, size_t n) { proc->pushTs(p, n); };
    }
    m_rx.setCallbacks(cb);
    return m_rx.start(cfg);
}

void ReceiverController::stop()
{
    m_rx.stop();
}

void ReceiverController::retune(const tbs::TuningParams &tuning)
{
    m_rx.retune(tuning);
}

bool ReceiverController::isRunning() const
{
    return m_rx.isRunning();
}
