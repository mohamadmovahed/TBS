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
