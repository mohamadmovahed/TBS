#pragma once

#include "FrontendStatus.h"
#include "TunerConfig.h"

#include <QString>
#include <cstddef>
#include <cstdint>
#include <functional>

// Abstraction of a receiver backend. All methods are called from the
// ReceiverEngine worker thread.
class ITunerDevice
{
public:
    using LogFn = std::function<void(const QString &)>;

    virtual ~ITunerDevice() = default;

    virtual QString name() const = 0;

    // Opens the device and tunes it according to cfg.
    virtual bool open(const TunerConfig &cfg, QString *error) = 0;
    // Starts delivery of the transport stream.
    virtual bool startStream(QString *error) = 0;
    // Reads up to maxLen bytes. Returns the number of bytes read (0 on timeout)
    // or -1 on a fatal error.
    virtual long read(uint8_t *buf, size_t maxLen, int timeoutMs) = 0;
    virtual FrontendStatus readStatus() = 0;
    virtual void close() = 0;

    // Number of buffer overflows reported by the driver.
    virtual uint64_t overflows() const { return 0; }
    // True when the device has nothing more to deliver (file replay without loop).
    virtual bool atEnd() const { return false; }

    void setLogger(LogFn fn) { m_log = std::move(fn); }

protected:
    void log(const QString &msg) const
    {
        if (m_log)
            m_log(msg);
    }

private:
    LogFn m_log;
};
