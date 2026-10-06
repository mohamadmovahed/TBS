#pragma once

#include "ITunerDevice.h"

#include <QElapsedTimer>
#include <QFile>

// Replays a recorded transport stream / pseudo-TS file. Available on every
// platform; useful to test outputs and the UI without hardware.
class FileReplayDevice : public ITunerDevice
{
public:
    QString name() const override { return QStringLiteral("File replay"); }
    bool open(const TunerConfig &cfg, QString *error) override;
    bool startStream(QString *error) override;
    long read(uint8_t *buf, size_t maxLen, int timeoutMs) override;
    FrontendStatus readStatus() override;
    void close() override;
    bool atEnd() const override { return m_atEnd; }

private:
    QFile m_file;
    double m_rateBytesPerSec = 0.0;
    bool m_loop = true;
    bool m_atEnd = false;
    QElapsedTimer m_clock;
    uint64_t m_delivered = 0;
};
