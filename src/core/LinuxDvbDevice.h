#pragma once

#include "DeviceFactory.h"
#include "ITunerDevice.h"

#include <QList>

// Backend for the Linux DVB API (TBS open source driver "linux_media",
// STiD135 demodulator on the TBS 6903x). Uses
//   /dev/dvb/adapterN/frontendM  for tuning, SEC and status
//   /dev/dvb/adapterN/demuxM     with a 0x2000 (all PIDs) filter in TSDEMUX_TAP mode for data
class LinuxDvbDevice : public ITunerDevice
{
public:
    ~LinuxDvbDevice() override;

    static QList<DeviceInfo> enumerate();

    QString name() const override { return m_name; }
    bool open(const TunerConfig &cfg, QString *error) override;
    bool startStream(QString *error) override;
    long read(uint8_t *buf, size_t maxLen, int timeoutMs) override;
    FrontendStatus readStatus() override;
    void close() override;
    uint64_t overflows() const override { return m_overflows; }

private:
    bool setupSec(QString *error);
    bool tune(QString *error);
    void readSignalStatistics(FrontendStatus &st);
    void readTuningParameters(FrontendStatus &st);

    TunerConfig m_cfg;
    QString m_name = QStringLiteral("DVB frontend");
    int m_fe = -1;
    int m_dmx = -1;
    uint64_t m_overflows = 0;
    bool m_tempSupported = true;

    // For BER computed from the cumulative DVBv5 counters
    uint64_t m_lastErrBits = 0;
    uint64_t m_lastTotalBits = 0;
    bool m_haveBerCounters = false;
    double m_lastBer = 0.0;
};
