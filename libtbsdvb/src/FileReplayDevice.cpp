#include "FileReplayDevice.h"

#include "Util.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace tbs {

FileReplayDevice::~FileReplayDevice()
{
    close();
}

bool FileReplayDevice::open(const DeviceSelection &sel, const TuningParams &, CaptureMode, std::string *error)
{
    close();
    m_file = util::openFile(sel.replayFile, "rb");
    if (!m_file) {
        if (error)
            *error = util::format("Cannot open replay file '%s': %s", sel.replayFile.c_str(), std::strerror(errno));
        return false;
    }
    m_rateBytesPerSec = sel.replayRateMbps * 1e6 / 8.0;
    m_loop = sel.replayLoop;
    m_atEnd = false;
    log(util::format("Replaying %s at %s", sel.replayFile.c_str(),
                     m_rateBytesPerSec > 0 ? util::format("%.3f Mbit/s", sel.replayRateMbps).c_str() : "full speed"));
    return true;
}

bool FileReplayDevice::startStream(std::string *)
{
    m_startMs = util::nowMs();
    m_delivered = 0;
    return true;
}

long FileReplayDevice::read(uint8_t *buf, size_t maxLen, int timeoutMs)
{
    if (!m_file)
        return -1;
    if (m_atEnd) {
        util::sleepMs(timeoutMs);
        return 0;
    }

    size_t want = maxLen;
    if (m_rateBytesPerSec > 0) {
        const double allowed = m_rateBytesPerSec * static_cast<double>(util::nowMs() - m_startMs) / 1000.0;
        if (allowed <= static_cast<double>(m_delivered)) {
            const double waitMs = (static_cast<double>(m_delivered) - allowed) / m_rateBytesPerSec * 1000.0 + 1.0;
            util::sleepMs(static_cast<int>(std::min<double>(waitMs, timeoutMs)));
            return 0;
        }
        want = std::min<size_t>(want, static_cast<size_t>(allowed - static_cast<double>(m_delivered)) + 188);
    }

    size_t n = std::fread(buf, 1, want, m_file);
    if (n == 0) {
        if (std::ferror(m_file))
            return -1;
        if (!m_loop) {
            m_atEnd = true;
            log("End of replay file");
            return 0;
        }
        std::rewind(m_file);
        n = std::fread(buf, 1, want, m_file);
    }
    m_delivered += n;
    return static_cast<long>(n);
}

FrontendStatus FileReplayDevice::readStatus()
{
    FrontendStatus st;
    st.valid = true;
    st.deviceName = name();
    if (m_file && !m_atEnd)
        st.flags = FrontendStatus::HasSignal | FrontendStatus::HasCarrier | FrontendStatus::HasViterbi
            | FrontendStatus::HasSync | FrontendStatus::HasLock;
    return st;
}

void FileReplayDevice::close()
{
    if (m_file) {
        std::fclose(m_file);
        m_file = nullptr;
    }
}

} // namespace tbs
