#pragma once

#include <tbsdvb/Receiver.h>

#include <QMetaType>
#include <QObject>
#include <functional>

Q_DECLARE_METATYPE(tbs::FrontendStatus)
Q_DECLARE_METATYPE(tbs::StreamStats)

// Qt adapter for tbs::Receiver: library callbacks (worker thread) become
// queued Qt signals delivered on the GUI thread.
class ReceiverController : public QObject
{
    Q_OBJECT

public:
    explicit ReceiverController(QObject *parent = nullptr);
    ~ReceiverController() override;

    // Called on the receiver thread with decodable TS while preview is enabled
    void setPreviewSink(std::function<void(const uint8_t *, size_t)> sink);
    void setPreviewEnabled(bool on);

    bool start(const tbs::ReceiverConfig &cfg);
    void stop();
    void retune(const tbs::TuningParams &tuning);
    bool isRunning() const;

    tbs::Receiver &receiver() { return m_rx; }

signals:
    void stateChanged(int state, const QString &error);
    void logMessage(const QString &message);
    void statusUpdated(const tbs::FrontendStatus &status);
    void statsUpdated(const tbs::StreamStats &stats);

private:
    tbs::Receiver m_rx;
    std::function<void(const uint8_t *, size_t)> m_previewSink;
};
