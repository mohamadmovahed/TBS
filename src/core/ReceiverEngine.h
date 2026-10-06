#pragma once

#include "FrontendStatus.h"
#include "StreamStats.h"
#include "TunerConfig.h"

#include <QObject>
#include <atomic>
#include <thread>

class ITsConsumer;

// Runs the capture pipeline on a worker thread:
//   device -> TS alignment -> (BBFrame reassembly) -> file / ZeroMQ sinks
//                                                  -> preview consumer (video)
// Signals are delivered to the GUI thread through queued connections.
class ReceiverEngine : public QObject
{
    Q_OBJECT

public:
    explicit ReceiverEngine(QObject *parent = nullptr);
    ~ReceiverEngine() override;

    bool start(const TunerConfig &cfg);
    void stop();
    bool isRunning() const { return m_running.load(); }

    // The consumer must outlive the engine run (or be reset to nullptr first).
    void setPreviewConsumer(ITsConsumer *consumer) { m_preview.store(consumer); }

signals:
    void started();
    void stopped();
    void failed(const QString &message);
    void logMessage(const QString &message);
    void frontendStatusChanged(const FrontendStatus &status);
    void streamStatsChanged(const StreamStats &stats);

private:
    void run(TunerConfig cfg);

    std::thread m_thread;
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_running{false};
    std::atomic<ITsConsumer *> m_preview{nullptr};
};
