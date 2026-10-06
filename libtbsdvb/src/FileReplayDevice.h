#pragma once

#include "tbsdvb/Device.h"

#include <cstdio>

namespace tbs {

// Replays a recorded transport stream / pseudo-TS file (all platforms).
class FileReplayDevice : public ITunerDevice
{
public:
    ~FileReplayDevice() override;

    std::string name() const override { return "File replay"; }
    bool open(const DeviceSelection &sel, const TuningParams &tuning, CaptureMode mode, std::string *error) override;
    bool tune(const TuningParams &, std::string *) override { return true; }
    bool startStream(std::string *error) override;
    long read(uint8_t *buf, size_t maxLen, int timeoutMs) override;
    FrontendStatus readStatus() override;
    void close() override;
    bool atEnd() const override { return m_atEnd; }

private:
    std::FILE *m_file = nullptr;
    double m_rateBytesPerSec = 0.0;
    bool m_loop = true;
    bool m_atEnd = false;
    int64_t m_startMs = 0;
    uint64_t m_delivered = 0;
};

} // namespace tbs
