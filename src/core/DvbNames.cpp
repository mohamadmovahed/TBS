#include "DvbNames.h"

#include "FrontendStatus.h"

#include <QStringList>
#include <iterator>

namespace {
template <size_t N>
QString lookup(const char *const (&table)[N], int v)
{
    if (v < 0)
        return QStringLiteral("-");
    if (static_cast<size_t>(v) < N && table[v])
        return QString::fromLatin1(table[v]);
    return QStringLiteral("#%1").arg(v);
}
} // namespace

namespace DvbNames {

QString deliverySystem(int v)
{
    static const char *const t[] = {"Undefined", "DVB-C/A", "DVB-C/B", "DVB-T", "DSS", "DVB-S",
                                    "DVB-S2", "DVB-H", "ISDB-T", "ISDB-S", "ISDB-C", "ATSC",
                                    "ATSC-MH", "DTMB", "CMMB", "DAB", "DVB-T2", "Turbo",
                                    "DVB-C/C", "DVB-C2"};
    return lookup(t, v);
}

QString modulation(int v)
{
    static const char *const t[] = {"QPSK", "16QAM", "32QAM", "64QAM", "128QAM", "256QAM", "Auto",
                                    "8VSB", "16VSB", "8PSK", "16APSK", "32APSK", "DQPSK", "4QAM-NR",
                                    "1024QAM", "4096QAM", "8APSK-L", "16APSK-L", "32APSK-L", "64APSK",
                                    "64APSK-L", "512QAM", "128APSK", "256APSK", "128APSK-L",
                                    "256APSK-L", "1024APSK"};
    return lookup(t, v);
}

QString fec(int v)
{
    static const char *const t[] = {"None", "1/2", "2/3", "3/4", "4/5", "5/6", "6/7", "7/8", "8/9",
                                    "Auto", "3/5", "9/10", "2/5", "1/3", "1/4", "5/9", "7/9", "8/15",
                                    "11/15", "13/18", "9/20", "11/20", "23/36", "25/36", "13/45",
                                    "26/45", "28/45", "32/45", "77/90", "11/45", "4/15", "14/45",
                                    "7/15", "29/45", "31/45"};
    return lookup(t, v);
}

QString rolloff(int v)
{
    static const char *const t[] = {"0.35", "0.20", "0.25", "Auto", "0.15", "0.10", "0.05"};
    return lookup(t, v);
}

QString pilot(int v)
{
    static const char *const t[] = {"On", "Off", "Auto"};
    return lookup(t, v);
}

QString statusFlags(uint32_t flags)
{
    QStringList parts;
    if (flags & FrontendStatus::HasSignal)
        parts << QStringLiteral("SIGNAL");
    if (flags & FrontendStatus::HasCarrier)
        parts << QStringLiteral("CARRIER");
    if (flags & FrontendStatus::HasViterbi)
        parts << QStringLiteral("FEC");
    if (flags & FrontendStatus::HasSync)
        parts << QStringLiteral("SYNC");
    if (flags & FrontendStatus::HasLock)
        parts << QStringLiteral("LOCK");
    if (flags & FrontendStatus::TimedOut)
        parts << QStringLiteral("TIMEDOUT");
    return parts.isEmpty() ? QStringLiteral("none") : parts.join(QLatin1Char(' '));
}

} // namespace DvbNames
