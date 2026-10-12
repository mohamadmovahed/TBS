#include "tbsdvb/Sink.h"

#include "Util.h"
#include "tbsdvb/Status.h"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <system_error>

namespace tbs {

namespace fs = std::filesystem;

IOutputSink::~IOutputSink() = default;

FileSink::FileSink(const FileOutputOptions &opt, CaptureMode mode)
    : m_opt(opt)
    , m_mode(mode)
{
}

FileSink::~FileSink()
{
    close();
}

std::string FileSink::resolvePath(const FileOutputOptions &opt, CaptureMode mode)
{
    const bool bb = mode == CaptureMode::BBFrame && opt.bbFormat != BbFileFormat::PseudoTs;
    const std::string defaultExt = bb ? ".bbf" : ".ts";
    fs::path p = opt.path.empty() ? fs::u8path(util::homeDirectory()) / ("capture" + defaultExt) : fs::u8path(opt.path);

    std::string ext = p.extension().u8string();
    if (ext.empty())
        ext = defaultExt;
    std::string stem = p.stem().u8string();
    if (opt.timestampSuffix) {
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "_%Y%m%d_%H%M%S", &tm);
        stem += buf;
    }
    return (p.parent_path() / fs::u8path(stem + ext)).u8string();
}

std::string FileSink::segmentPath(const std::string &base, uint32_t n)
{
    const fs::path p = fs::u8path(base);
    return (p.parent_path() / fs::u8path(p.stem().u8string() + util::format("_%03u", n) + p.extension().u8string())).u8string();
}

bool FileSink::open(std::string *error)
{
    close();
    m_base = resolvePath(m_opt, m_mode);
    m_bytes = 0;
    m_segment = 0;
    m_written.clear();
    m_error.clear();
    m_stopped = false;
    std::error_code ec;
    const fs::path parent = fs::u8path(m_base).parent_path();
    if (!parent.empty())
        fs::create_directories(parent, ec);

    m_lastSpaceCheckMs = 0;
    checkFreeSpace();
    if (m_stopped) {
        if (error)
            *error = m_error;
        return false;
    }
    return openSegment(error);
}

bool FileSink::openSegment(std::string *error)
{
    const bool splitting = m_opt.splitSizeMB > 0 || m_opt.splitMinutes > 0;
    ++m_segment;
    m_path = splitting ? segmentPath(m_base, m_segment) : m_base;
    m_file = util::openFile(m_path, "wb");
    if (!m_file) {
        const std::string msg = "Cannot create '" + m_path + "': " + std::strerror(errno);
        if (error)
            *error = msg;
        m_error = msg;
        m_stopped = true;
        return false;
    }
    m_buffer.resize(4u << 20);
    std::setvbuf(m_file, m_buffer.data(), _IOFBF, m_buffer.size());
    m_segmentBytes = 0;
    m_segmentStartMs = util::nowMs();
    m_written.push_back(m_path);

    // Rolling recording: delete the oldest segments beyond the limit
    if (splitting && m_opt.keepFiles > 0) {
        while (m_written.size() > static_cast<size_t>(m_opt.keepFiles)) {
            std::error_code ec;
            fs::remove(fs::u8path(m_written.front()), ec);
            m_written.erase(m_written.begin());
        }
    }
    return true;
}

void FileSink::close()
{
    if (m_file) {
        std::fclose(m_file);
        m_file = nullptr;
    }
}

void FileSink::stop(const std::string &reason)
{
    if (m_stopped)
        return;
    m_stopped = true;
    m_error = reason;
    close();
}

void FileSink::checkFreeSpace()
{
    const int64_t now = util::nowMs();
    if (m_lastSpaceCheckMs && now - m_lastSpaceCheckMs < 2000)
        return;
    m_lastSpaceCheckMs = now;
    std::error_code ec;
    const fs::path dir = fs::u8path(m_base).parent_path().empty() ? fs::current_path(ec) : fs::u8path(m_base).parent_path();
    const fs::space_info si = fs::space(dir, ec);
    if (ec)
        return; // unknown, keep recording
    m_freeBytes = static_cast<int64_t>(si.available);
    if (m_opt.minFreeMB > 0 && si.available < (m_opt.minFreeMB << 20))
        stop(util::format("free disk space below %llu MiB (%llu MiB left)", static_cast<unsigned long long>(m_opt.minFreeMB),
                          static_cast<unsigned long long>(si.available >> 20)));
}

void FileSink::maybeRotate()
{
    if (!m_file)
        return;
    const bool bySize = m_opt.splitSizeMB > 0 && m_segmentBytes >= (m_opt.splitSizeMB << 20);
    const bool byTime = m_opt.splitMinutes > 0 && util::nowMs() - m_segmentStartMs >= m_opt.splitMinutes * 60000LL;
    if (!bySize && !byTime)
        return;
    close();
    std::string err;
    if (!openSegment(&err))
        stop(err);
}

void FileSink::fillStats(StreamStats &st)
{
    if (m_file)
        checkFreeSpace();
    st.fileBytes = m_bytes;
    st.fileName = m_path;
    st.fileSegments = m_segment;
    st.fileStopped = m_stopped;
    st.fileFreeBytes = m_freeBytes;
}

bool FileSink::acceptsTs(CaptureMode mode) const
{
    return mode == CaptureMode::TransportStream || m_opt.bbFormat == BbFileFormat::PseudoTs;
}

bool FileSink::acceptsBbFrames(CaptureMode mode) const
{
    return mode == CaptureMode::BBFrame && m_opt.bbFormat != BbFileFormat::PseudoTs;
}

void FileSink::write(const void *data, size_t len)
{
    if (!m_file)
        return;
    const size_t n = std::fwrite(data, 1, len, m_file);
    m_bytes += n;
    m_segmentBytes += n;
    if (n != len)
        stop(std::string("write failed: ") + std::strerror(errno));
}

void FileSink::writeTs(const uint8_t *packets, size_t count)
{
    maybeRotate();
    write(packets, count * 188);
}

void FileSink::writeBbFrame(const uint8_t *frame, size_t len, const BbHeader &)
{
    maybeRotate(); // segments always start on a frame boundary
    if (m_opt.bbFormat == BbFileFormat::B8Marker) {
        const uint8_t marker = 0xB8;
        write(&marker, 1);
    }
    write(frame, len);
}

} // namespace tbs
