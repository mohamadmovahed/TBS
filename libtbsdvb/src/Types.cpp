#include "tbsdvb/Types.h"

#include "Util.h"
#include "tbsdvb/PlsUtil.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

#ifndef TBSDVB_VERSION
#define TBSDVB_VERSION "0.0.0"
#endif

namespace tbs {

const char *version()
{
    return TBSDVB_VERSION;
}

bool TuningParams::isHighBand() const
{
    return lnbType == LnbType::Universal && frequencyMHz >= lnbSwitchMHz;
}

double TuningParams::activeLoMHz() const
{
    switch (lnbType) {
    case LnbType::Direct:
        return 0.0;
    case LnbType::Universal:
        return isHighBand() ? lnbLo2MHz : lnbLo1MHz;
    case LnbType::SingleLo:
        return lnbLo1MHz;
    }
    return 0.0;
}

uint32_t TuningParams::intermediateFrequencyKHz() const
{
    const double ifMHz = lnbType == LnbType::Direct ? frequencyMHz : std::fabs(frequencyMHz - activeLoMHz());
    return static_cast<uint32_t>(std::lround(ifMHz * 1000.0));
}

bool TuningParams::toneOn() const
{
    switch (tone) {
    case ToneMode::On:
        return true;
    case ToneMode::Off:
        return false;
    case ToneMode::Auto:
        break;
    }
    return isHighBand();
}

uint32_t TuningParams::goldCode() const
{
    if (plsMode == PlsMode::Root)
        return PlsUtil::rootToGold(plsCode);
    return plsCode & 0x3FFFF;
}

uint32_t TuningParams::dvbStreamId() const
{
    if (inputStreamId < 0)
        return ~0u;
    return (static_cast<uint32_t>(inputStreamId) & 0xFF) | ((plsCode & 0x3FFFF) << 8)
        | ((static_cast<uint32_t>(plsMode) & 0x3) << 26);
}

std::string TuningParams::describe() const
{
    static const char *pols[] = {"off", "V/R", "H/L"};
    std::string s = util::format("%.3f MHz %s %.1f kS/s, IF %.3f MHz, tone %s", frequencyMHz,
                                 pols[static_cast<int>(polarization)], symbolRateKsps,
                                 intermediateFrequencyKHz() / 1000.0, toneOn() ? "on" : "off");
    if (diseqcPort >= 0)
        s += util::format(", DiSEqC %c", 'A' + diseqcPort);
    if (inputStreamId >= 0)
        s += util::format(", ISI %d", inputStreamId);
    if (goldCode() != 0)
        s += util::format(", PLS gold %u", goldCode());
    return s;
}

std::string DeviceInfo::label() const
{
    switch (kind) {
    case DeviceKind::LinuxDvb:
        return util::format("adapter%d/frontend%d: %s", adapter, frontend, name.c_str());
    case DeviceKind::WindowsBda:
        return util::format("BDA #%d: %s", index, name.c_str());
    case DeviceKind::FileReplay:
        break;
    }
    return name;
}

void DeviceInfo::applyTo(DeviceSelection &sel) const
{
    sel.kind = kind;
    switch (kind) {
    case DeviceKind::LinuxDvb:
        sel.adapter = adapter;
        sel.frontend = frontend;
        break;
    case DeviceKind::WindowsBda:
        sel.bdaIndex = index;
        sel.bdaDevicePath = path;
        break;
    case DeviceKind::FileReplay:
        break;
    }
}

// ---- Network output URLs --------------------------------------------------

namespace {

std::string urlDecode(const std::string &in)
{
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size() && std::isxdigit(static_cast<unsigned char>(in[i + 1]))
            && std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
            out += static_cast<char>(std::strtol(in.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else if (in[i] == '+') {
            out += ' ';
        } else {
            out += in[i];
        }
    }
    return out;
}

std::string urlEncode(const std::string &in)
{
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : in) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == ':') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

bool parseInt(const std::string &v, long lo, long hi, long *out)
{
    if (v.empty())
        return false;
    char *end = nullptr;
    const long x = std::strtol(v.c_str(), &end, 0);
    if (*end != '\0' || x < lo || x > hi)
        return false;
    *out = x;
    return true;
}

bool fail(std::string *error, const std::string &msg)
{
    if (error)
        *error = msg;
    return false;
}

} // namespace

bool parseNetworkUrl(const std::string &url, TsNetworkOutput *out, std::string *error)
{
    TsNetworkOutput o;
    const size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos)
        return fail(error, "expected udp://, rtp:// or srt:// in \"" + url + "\"");
    std::string scheme = url.substr(0, schemeEnd);
    for (char &c : scheme)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (scheme == "udp")
        o.protocol = NetProtocol::Udp;
    else if (scheme == "rtp")
        o.protocol = NetProtocol::Rtp;
    else if (scheme == "srt")
        o.protocol = NetProtocol::Srt;
    else
        return fail(error, "unknown protocol \"" + scheme + "\" (udp, rtp or srt)");

    std::string rest = url.substr(schemeEnd + 3);
    std::string query;
    const size_t q = rest.find('?');
    if (q != std::string::npos) {
        query = rest.substr(q + 1);
        rest = rest.substr(0, q);
    }
    if (!rest.empty() && rest.back() == '/')
        rest.pop_back();

    // host:port, [v6]:port or :port
    std::string host, port;
    if (!rest.empty() && rest[0] == '[') {
        const size_t close = rest.find(']');
        if (close == std::string::npos || close + 1 >= rest.size() || rest[close + 1] != ':')
            return fail(error, "expected [address]:port in \"" + url + "\"");
        host = rest.substr(1, close - 1);
        port = rest.substr(close + 2);
    } else {
        const size_t colon = rest.rfind(':');
        if (colon == std::string::npos)
            return fail(error, "missing port in \"" + url + "\"");
        host = rest.substr(0, colon);
        port = rest.substr(colon + 1);
    }
    long v = 0;
    if (!parseInt(port, 1, 65535, &v))
        return fail(error, "invalid port \"" + port + "\"");
    o.port = static_cast<uint16_t>(v);
    o.host = host;

    bool listener = false;
    std::istringstream qs(query);
    std::string kv;
    while (std::getline(qs, kv, '&')) {
        if (kv.empty())
            continue;
        const size_t eq = kv.find('=');
        const std::string key = kv.substr(0, eq);
        const std::string val = eq == std::string::npos ? std::string() : urlDecode(kv.substr(eq + 1));
        if (key == "program") {
            if (!parseInt(val, 0, 65535, &v))
                return fail(error, "invalid program \"" + val + "\"");
            o.programNumber = static_cast<int>(v);
        } else if (key == "pids") {
            std::istringstream ps(val);
            std::string pid;
            while (std::getline(ps, pid, ',')) {
                if (!parseInt(pid, 0, 0x1FFF, &v))
                    return fail(error, "invalid PID \"" + pid + "\"");
                o.pids.push_back(static_cast<uint16_t>(v));
            }
        } else if (key == "isi") {
            if (!parseInt(val, -1, 255, &v))
                return fail(error, "invalid isi \"" + val + "\"");
            o.isi = static_cast<int>(v);
        } else if (key == "ttl") {
            if (!parseInt(val, 0, 255, &v))
                return fail(error, "invalid ttl \"" + val + "\"");
            o.ttl = static_cast<int>(v);
        } else if (key == "pkts") {
            if (!parseInt(val, 1, 7, &v))
                return fail(error, "pkts must be 1..7");
            o.packetsPerDatagram = static_cast<int>(v);
        } else if (key == "iface" || key == "localaddr") {
            o.interfaceName = val;
        } else if (key == "null") {
            o.dropNullPackets = val == "0";
        } else if (key == "mode") {
            if (val == "listener" || val == "server")
                listener = true;
            else if (val != "caller" && val != "client")
                return fail(error, "SRT mode must be caller or listener");
        } else if (key == "latency") {
            if (!parseInt(val, 0, 60000, &v))
                return fail(error, "invalid latency \"" + val + "\"");
            o.srtLatencyMs = static_cast<int>(v);
        } else if (key == "passphrase") {
            if (!val.empty() && (val.size() < 10 || val.size() > 79))
                return fail(error, "SRT passphrase must be 10 to 79 characters");
            o.srtPassphrase = val;
        } else if (key == "streamid") {
            o.srtStreamId = val;
        } else {
            return fail(error, "unknown parameter \"" + key + "\"");
        }
    }
    o.srtMode = listener ? SrtMode::Listener : SrtMode::Caller;
    if (o.host.empty() && !(o.protocol == NetProtocol::Srt && listener))
        return fail(error, "missing destination address in \"" + url + "\"");
    if (out)
        *out = o;
    return true;
}

std::string networkUrl(const TsNetworkOutput &o, bool redactSecrets)
{
    const TsNetworkOutput def;
    std::string s = o.protocol == NetProtocol::Rtp ? "rtp://" : o.protocol == NetProtocol::Srt ? "srt://" : "udp://";
    s += o.host.find(':') != std::string::npos ? "[" + o.host + "]" : o.host;
    s += ":" + std::to_string(o.port);
    std::vector<std::string> q;
    if (o.protocol == NetProtocol::Srt) {
        if (o.srtMode == SrtMode::Listener)
            q.push_back("mode=listener");
        if (o.srtLatencyMs != def.srtLatencyMs)
            q.push_back("latency=" + std::to_string(o.srtLatencyMs));
        if (!o.srtPassphrase.empty())
            q.push_back("passphrase=" + (redactSecrets ? std::string("***") : urlEncode(o.srtPassphrase)));
        if (!o.srtStreamId.empty())
            q.push_back("streamid=" + urlEncode(o.srtStreamId));
    } else {
        if (o.ttl != def.ttl)
            q.push_back("ttl=" + std::to_string(o.ttl));
        if (!o.interfaceName.empty())
            q.push_back("iface=" + urlEncode(o.interfaceName));
    }
    if (o.programNumber > 0)
        q.push_back("program=" + std::to_string(o.programNumber));
    if (!o.pids.empty()) {
        std::string p = "pids=";
        for (size_t i = 0; i < o.pids.size(); ++i)
            p += (i ? "," : "") + util::format("0x%04x", o.pids[i]);
        q.push_back(p);
    }
    if (o.isi >= 0)
        q.push_back("isi=" + std::to_string(o.isi));
    if (o.packetsPerDatagram != def.packetsPerDatagram)
        q.push_back("pkts=" + std::to_string(o.packetsPerDatagram));
    if (!o.dropNullPackets)
        q.push_back("null=1");
    for (size_t i = 0; i < q.size(); ++i)
        s += (i ? "&" : "?") + q[i];
    return s;
}

} // namespace tbs
