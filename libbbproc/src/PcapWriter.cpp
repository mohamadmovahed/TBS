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

    m_packets = 0;
    m_bytes = 0;
    m_error.clear();
    m_interfaces.clear();
    if (m_opt.format == PcapFormat::PcapNg) {
        // Section Header Block
        uint8_t shb[28];
        put32(shb, 0x0A0D0D0Au);
        put32(shb + 4, sizeof(shb));
        put32(shb + 8, 0x1A2B3C4Du); // byte-order magic
        shb[12] = 1;                 // version 1.0
        shb[13] = 0;
        shb[14] = 0;
        shb[15] = 0;
        std::memset(shb + 16, 0xFF, 8); // section length unknown
        put32(shb + 24, sizeof(shb));
        put(shb, sizeof(shb));
        return true;
    }
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
    put(hdr, sizeof(hdr));
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

bool PcapWriter::put(const void *p, size_t n)
{
    if (!m_file)
        return false;
    if (n && std::fwrite(p, 1, n, m_file) != n) {
        m_error = std::strerror(errno);
        close();
        return false;
    }
    m_bytes += n;
    return true;
}

uint32_t PcapWriter::interfaceId(int key, const std::string &name)
{
    auto it = m_interfaces.find(key);
    if (it != m_interfaces.end())
        return it->second;
    const uint32_t id = static_cast<uint32_t>(m_interfaces.size());
    m_interfaces[key] = id;

    // Interface Description Block with if_name
    const std::string ifName = name.empty() ? "stream " + std::to_string(key) : name;
    const size_t nameLen = ifName.size();
    const size_t namePadded = (nameLen + 3) & ~size_t(3);
    const size_t len = 16 + 4 + namePadded + 4 + 4; // header, if_name option, opt_endofopt, trailer
    std::vector<uint8_t> b(len, 0);
    put32(b.data(), 0x00000001u);
    put32(b.data() + 4, static_cast<uint32_t>(len));
    const uint16_t link = m_opt.ethernetFraming ? kLinkEthernet : kLinkRaw;
    b[8] = static_cast<uint8_t>(link);
    b[9] = static_cast<uint8_t>(link >> 8);
    put32(b.data() + 12, kSnapLen);
    b[16] = 2; // if_name
    b[18] = static_cast<uint8_t>(nameLen);
    b[19] = static_cast<uint8_t>(nameLen >> 8);
    std::memcpy(b.data() + 20, ifName.data(), nameLen);
    // opt_endofopt is already zero
    put32(b.data() + len - 4, static_cast<uint32_t>(len));
    put(b.data(), b.size());
    return id;
}

void PcapWriter::writeRecord(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen, int64_t unixMicros, uint32_t iface)
{
    const size_t total = alen + blen;
    const size_t incl = std::min<size_t>(total, kSnapLen);
    const size_t aw = std::min(alen, incl);
    const size_t bw = incl - aw;
    bool ok;
    if (m_opt.format == PcapFormat::PcapNg) {
        // Enhanced Packet Block, microsecond timestamps (default if_tsresol)
        const size_t padded = (incl + 3) & ~size_t(3);
        const uint32_t len = static_cast<uint32_t>(28 + padded + 4);
        uint8_t h[28];
        put32(h, 0x00000006u);
        put32(h + 4, len);
        put32(h + 8, iface);
        const uint64_t ts = static_cast<uint64_t>(unixMicros);
        put32(h + 12, static_cast<uint32_t>(ts >> 32));
        put32(h + 16, static_cast<uint32_t>(ts));
        put32(h + 20, static_cast<uint32_t>(incl));
        put32(h + 24, static_cast<uint32_t>(total));
        static const uint8_t zeros[4] = {};
        uint8_t trailer[4];
        put32(trailer, len);
        ok = put(h, sizeof(h)) && put(a, aw) && put(b, bw) && put(zeros, padded - incl) && put(trailer, 4);
    } else {
        uint8_t rec[16];
        put32(rec, static_cast<uint32_t>(unixMicros / 1000000));
        put32(rec + 4, static_cast<uint32_t>(unixMicros % 1000000));
        put32(rec + 8, static_cast<uint32_t>(incl));
        put32(rec + 12, static_cast<uint32_t>(total));
        ok = put(rec, sizeof(rec)) && put(a, aw) && put(b, bw);
    }
    if (ok)
        ++m_packets;
}

bool PcapWriter::write(const GsePdu &pdu, int64_t unixMicros, int interfaceKey, const std::string &interfaceName)
{
    if (!m_file)
        return false;
    const bool ip = pdu.protocolType == 0x0800 || pdu.protocolType == 0x86DD;
    const uint32_t iface = m_opt.format == PcapFormat::PcapNg ? interfaceId(interfaceKey, interfaceName) : 0;

    if (!m_opt.ethernetFraming) {
        if (!ip)
            return false;
        if (pdu.bridged) {
            if (pdu.len < 14)
                return false;
            writeRecord(pdu.data + 14, pdu.len - 14, nullptr, 0, unixMicros, iface);
        } else {
            writeRecord(pdu.data, pdu.len, nullptr, 0, unixMicros, iface);
        }
        return m_file != nullptr;
    }

    if (m_opt.ipOnly && !ip)
        return false;
    if (pdu.bridged) {
        writeRecord(pdu.data, pdu.len, nullptr, 0, unixMicros, iface);
        return m_file != nullptr;
    }
    uint8_t eth[14];
    ethernetHeaderFor(pdu, eth);
    writeRecord(eth, sizeof(eth), pdu.data, pdu.len, unixMicros, iface);
    return m_file != nullptr;
}

} // namespace bb
} // namespace tbs
