#include "tbsdvb/Sink.h"

#include "Util.h"

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

bool FileSink::open(std::string *error)
{
    close();
    m_path = resolvePath(m_opt, m_mode);
    std::error_code ec;
    const fs::path parent = fs::u8path(m_path).parent_path();
    if (!parent.empty())
        fs::create_directories(parent, ec);
    m_file = util::openFile(m_path, "wb");
    if (!m_file) {
        if (error)
            *error = "Cannot create '" + m_path + "': " + std::strerror(errno);
        return false;
    }
    m_buffer.resize(4u << 20);
    std::setvbuf(m_file, m_buffer.data(), _IOFBF, m_buffer.size());
    m_bytes = 0;
    m_error.clear();
    return true;
}

void FileSink::close()
{
    if (m_file) {
        std::fclose(m_file);
        m_file = nullptr;
    }
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
    if (n != len) {
        m_error = std::strerror(errno);
        close();
    }
}

void FileSink::writeTs(const uint8_t *packets, size_t count)
{
    write(packets, count * 188);
}

void FileSink::writeBbFrame(const uint8_t *frame, size_t len, const BbHeader &)
{
    if (m_opt.bbFormat == BbFileFormat::B8Marker) {
        const uint8_t marker = 0xB8;
        write(&marker, 1);
    }
    write(frame, len);
}

} // namespace tbs
