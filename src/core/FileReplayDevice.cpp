#include "FileReplayDevice.h"

#include <QFileInfo>
#include <QThread>
#include <algorithm>

bool FileReplayDevice::open(const TunerConfig &cfg, QString *error)
{
    m_file.setFileName(cfg.replayFile);
    if (!m_file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot open replay file '%1': %2").arg(cfg.replayFile, m_file.errorString());
        return false;
    }
    m_rateBytesPerSec = cfg.replayRateMbps * 1e6 / 8.0;
    m_loop = cfg.replayLoop;
    m_atEnd = false;
    log(QStringLiteral("Replaying %1 (%2 bytes) at %3")
            .arg(QFileInfo(cfg.replayFile).fileName())
            .arg(m_file.size())
            .arg(m_rateBytesPerSec > 0 ? QStringLiteral("%1 Mbit/s").arg(cfg.replayRateMbps) : QStringLiteral("full speed")));
    return true;
}

bool FileReplayDevice::startStream(QString *)
{
    m_clock.start();
    m_delivered = 0;
    return true;
}

long FileReplayDevice::read(uint8_t *buf, size_t maxLen, int timeoutMs)
{
    if (m_atEnd) {
        QThread::msleep(static_cast<unsigned long>(timeoutMs));
        return 0;
    }

    size_t want = maxLen;
    if (m_rateBytesPerSec > 0) {
        const double allowed = m_rateBytesPerSec * static_cast<double>(m_clock.nsecsElapsed()) / 1e9;
        if (allowed <= static_cast<double>(m_delivered)) {
            const double waitMs = (static_cast<double>(m_delivered) - allowed) / m_rateBytesPerSec * 1000.0 + 1.0;
            QThread::msleep(static_cast<unsigned long>(std::min<double>(waitMs, timeoutMs)));
            return 0;
        }
        want = std::min<size_t>(want, static_cast<size_t>(allowed - static_cast<double>(m_delivered)) + 188);
    }

    qint64 n = m_file.read(reinterpret_cast<char *>(buf), static_cast<qint64>(want));
    if (n == 0) {
        if (!m_loop) {
            m_atEnd = true;
            log(QStringLiteral("End of replay file"));
            return 0;
        }
        m_file.seek(0);
        n = m_file.read(reinterpret_cast<char *>(buf), static_cast<qint64>(want));
    }
    if (n < 0)
        return -1;
    m_delivered += static_cast<uint64_t>(n);
    return static_cast<long>(n);
}

FrontendStatus FileReplayDevice::readStatus()
{
    FrontendStatus st;
    st.valid = true;
    st.deviceName = name();
    st.flags = m_file.isOpen() && !m_atEnd
        ? (FrontendStatus::HasSignal | FrontendStatus::HasCarrier | FrontendStatus::HasViterbi
           | FrontendStatus::HasSync | FrontendStatus::HasLock)
        : 0;
    return st;
}

void FileReplayDevice::close()
{
    m_file.close();
}
