#include "tbsbbproc/PsiTypes.h"

#include <cstdio>
#include <ctime>

namespace tbs {
namespace bb {

namespace {

std::string xml(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default:
            if (c >= 0x20 || c == '\n' || c == '\t')
                out += static_cast<char>(c);
            break;
        }
    }
    return out;
}

std::string xmltvTime(int64_t t)
{
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d%02d +0000", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

// ISO 639-2 code as XMLTV lang attribute (lower case), empty if unknown
std::string langAttr(const std::string &l)
{
    if (l.size() != 3)
        return {};
    std::string out = " lang=\"";
    for (char c : l)
        out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return out + "\"";
}

} // namespace

std::string formatXmltv(const std::vector<ServiceSchedule> &schedules)
{
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                      "<!DOCTYPE tv SYSTEM \"xmltv.dtd\">\n"
                      "<tv generator-info-name=\"tbsbbproc\">\n";
    auto channelId = [](const ServiceSchedule &s) {
        return std::to_string(s.serviceId) + "." + std::to_string(s.tsid) + "." + std::to_string(s.onid) + ".dvb";
    };
    for (const ServiceSchedule &s : schedules) {
        out += "  <channel id=\"" + channelId(s) + "\">\n";
        out += "    <display-name>"
               + xml(s.serviceName.empty() ? "Service " + std::to_string(s.serviceId) : s.serviceName)
               + "</display-name>\n";
        out += "  </channel>\n";
    }
    for (const ServiceSchedule &s : schedules) {
        const std::string id = channelId(s);
        for (const EventInfo &e : s.events) {
            if (e.startUtc <= 0)
                continue;
            out += "  <programme start=\"" + xmltvTime(e.startUtc) + "\" stop=\""
                   + xmltvTime(e.startUtc + e.durationSec) + "\" channel=\"" + id + "\">\n";
            const std::string la = langAttr(e.language);
            out += "    <title" + la + ">" + xml(e.name.empty() ? std::string("(no title)") : e.name) + "</title>\n";
            if (!e.text.empty() && e.text != e.name)
                out += "    <sub-title" + la + ">" + xml(e.text) + "</sub-title>\n";
            std::string desc = e.extendedText;
            for (const auto &item : e.items)
                desc += (desc.empty() ? "" : "\n") + item.first + ": " + item.second;
            if (desc.empty() && !e.text.empty() && e.text.size() > 60)
                desc = e.text;
            if (!desc.empty())
                out += "    <desc" + la + ">" + xml(desc) + "</desc>\n";
            if (!e.genre.empty())
                out += "    <category lang=\"en\">" + xml(e.genre) + "</category>\n";
            if (e.parentalAge > 0)
                out += "    <rating system=\"" + xml(e.ratingCountry.empty() ? std::string("DVB") : e.ratingCountry)
                       + "\">\n      <value>" + std::to_string(e.parentalAge) + "</value>\n    </rating>\n";
            out += "  </programme>\n";
        }
    }
    out += "</tv>\n";
    return out;
}

} // namespace bb
} // namespace tbs
