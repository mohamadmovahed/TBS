#include "tbsbbproc/PcapWriter.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace tbs {
namespace bb {

namespace {

constexpr uint32_t kLinkEthernet = 1;
constexpr uint32_t kLinkRaw = 101;
constexpr uint32_t kSnapLen = 262144;

std::FILE *openUtf8(const std::string &path)
{
#if defined(_WIN32)
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], n);
    return _wfopen(w.c_str(), L"wb");
#else
    return std::fopen(path.c_str(), "wb");
#endif
}

std::string home()
{
#if defined(_WIN32)
    if (const char *p = std::getenv("USERPROFILE"))
        return p;
#endif
    if (const char *p = std::getenv("HOME"))
        return p;
    return ".";
}

void put32(uint8_t *p, uint32_t v)
{
    // pcap files are written in the host byte order; we always use little endian
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

} // namespace

PcapWriter::PcapWriter(const PcapOptions &opt)
    : m_opt(opt)
{
}

PcapWriter::~PcapWriter()
{
    close();
}

std::string PcapWriter::resolvePath(const PcapOptions &opt)
{
    namespace fs = std::filesystem;
    fs::path p = opt.path.empty() ? fs::u8path(home()) / "gse.pcap" : fs::u8path(opt.path);
    std::string ext = p.extension().u8string();
    if (ext.empty())
        ext = ".pcap";
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

bool PcapWriter::open(std::string *error)
{
    close();
    m_path = resolvePath(m_opt);
    std::error_code ec;
    const auto parent = std::filesystem::u8path(m_path).parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, ec);
    m_file = openUtf8(m_path);
    if (!m_file) {
        if (error)
            *error = "Cannot create '" + m_path + "': " + std::strerror(errno);
        return false;
    }
    m_buffer.resize(1u << 20);
    std::setvbuf(m_file, m_buffer.data(), _IOFBF, m_buffer.size());

    uint8_t hdr[24];
    put32(hdr, 0xA1B2C3D4u); // microsecond timestamps
    hdr[4] = 2;
    hdr[5] = 0; // version 2.4
    hdr[6] = 4;
    hdr[7] = 0;
    put32(hdr + 8, 0);  // thiszone
    put32(hdr + 12, 0); // sigfigs
    put32(hdr + 16, kSnapLen);
    put32(hdr + 20, m_opt.ethernetFraming ? kLinkEthernet : kLinkRaw);
    std::fwrite(hdr, 1, sizeof(hdr), m_file);
    m_packets = 0;
    m_bytes = sizeof(hdr);
    m_error.clear();
    return true;
}

void PcapWriter::close()
{
    if (m_file) {
        std::fclose(m_file);
        m_file = nullptr;
    }
}

void PcapWriter::flush()
{
    if (m_file)
        std::fflush(m_file);
}

void PcapWriter::writeRecord(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen, int64_t unixMicros)
{
    const size_t total = alen + blen;
    const size_t incl = std::min<size_t>(total, kSnapLen);
    uint8_t rec[16];
    put32(rec, static_cast<uint32_t>(unixMicros / 1000000));
    put32(rec + 4, static_cast<uint32_t>(unixMicros % 1000000));
    put32(rec + 8, static_cast<uint32_t>(incl));
    put32(rec + 12, static_cast<uint32_t>(total));
    bool ok = std::fwrite(rec, 1, sizeof(rec), m_file) == sizeof(rec);
    const size_t aw = std::min(alen, incl);
    if (aw)
        ok = ok && std::fwrite(a, 1, aw, m_file) == aw;
    const size_t bw = incl - aw;
    if (bw)
        ok = ok && std::fwrite(b, 1, bw, m_file) == bw;
    if (!ok) {
        m_error = std::strerror(errno);
        close();
        return;
    }
    ++m_packets;
    m_bytes += sizeof(rec) + incl;
}

bool PcapWriter::write(const GsePdu &pdu, int64_t unixMicros)
{
    if (!m_file)
        return false;
    const bool ip = pdu.protocolType == 0x0800 || pdu.protocolType == 0x86DD;

    if (!m_opt.ethernetFraming) {
        if (!ip)
            return false;
        if (pdu.bridged) {
            if (pdu.len < 14)
                return false;
            writeRecord(pdu.data + 14, pdu.len - 14, nullptr, 0, unixMicros);
        } else {
            writeRecord(pdu.data, pdu.len, nullptr, 0, unixMicros);
        }
        return m_file != nullptr;
    }

    if (m_opt.ipOnly && !ip)
        return false;
    if (pdu.bridged) {
        writeRecord(pdu.data, pdu.len, nullptr, 0, unixMicros);
        return m_file != nullptr;
    }
    // Synthetic Ethernet header: destination = GSE label (6-byte label is a MAC address)
    uint8_t eth[14] = {};
    if (pdu.labelLen == 6)
        std::memcpy(eth, pdu.label, 6);
    else if (pdu.labelLen == 3)
        std::memcpy(eth + 3, pdu.label, 3);
    else
        std::memset(eth, 0xFF, 6); // broadcast label
    eth[12] = static_cast<uint8_t>(pdu.protocolType >> 8);
    eth[13] = static_cast<uint8_t>(pdu.protocolType);
    writeRecord(eth, sizeof(eth), pdu.data, pdu.len, unixMicros);
    return m_file != nullptr;
}

} // namespace bb
} // namespace tbs
