#pragma once

#include "export.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tbs {
namespace bb {

// Results of the MPEG-2 PSI / DVB SI analysis of a transport stream.

struct TBSBBPROC_API CaInfo
{
    uint16_t systemId = 0;
    uint16_t pid = 0x1FFF; // ECM (PMT) or EMM (CAT) PID
    std::string systemName;
};

struct TBSBBPROC_API EsInfo
{
    uint16_t pid = 0x1FFF;
    uint8_t streamType = 0;
    std::string kind;     // Video, Audio, Subtitles, Teletext, Data, ...
    std::string codec;    // H.264, HEVC, AAC, AC-3, DVB subtitles, ...
    std::string language; // ISO 639 code(s)
    std::vector<CaInfo> ca;
};

struct TBSBBPROC_API ProgramInfo
{
    uint16_t programNumber = 0;
    uint16_t pmtPid = 0x1FFF;
    bool pmtSeen = false;
    int pmtVersion = -1;
    uint16_t pcrPid = 0x1FFF;
    std::vector<CaInfo> ca; // program level CA descriptors
    std::vector<EsInfo> streams;
};

struct TBSBBPROC_API EventInfo
{
    bool valid = false;
    uint16_t eventId = 0;
    int64_t startUtc = 0;     // seconds since 1970 (0 = undefined)
    uint32_t durationSec = 0;
    uint8_t runningStatus = 0;
    std::string language;
    std::string name;
    std::string text;          // short event descriptor
    std::string extendedText;  // extended event descriptors, joined
    std::vector<std::pair<std::string, std::string>> items; // extended event items (description, value)
    std::vector<uint8_t> content; // content descriptor: level 1 << 4 | level 2 nibbles
    std::string genre;            // name of the first content nibble pair
    int parentalAge = 0;          // minimum age from the parental rating descriptor (0 = none)
    std::string ratingCountry;
    bool scrambled = false;       // free_CA_mode
};

// EIT schedule (and present/following) events of one service, sorted by start time
struct TBSBBPROC_API ServiceSchedule
{
    uint16_t onid = 0;
    uint16_t tsid = 0;
    uint16_t serviceId = 0;
    std::string serviceName;   // from the SDT, if known
    bool actual = true;        // service of this transport stream
    std::vector<EventInfo> events;
};

struct TBSBBPROC_API ServiceInfo
{
    uint16_t serviceId = 0;
    uint16_t tsid = 0;
    uint16_t onid = 0;
    bool actual = true;       // from SDT actual (false = SDT other)
    uint8_t type = 0;
    std::string typeName;
    std::string name;
    std::string provider;
    uint8_t runningStatus = 0;
    bool scrambled = false;   // free_CA_mode
    bool eitPresentFollowing = false;
    bool eitSchedule = false;
    EventInfo present;
    EventInfo following;
};

struct TBSBBPROC_API TransportInfo
{
    uint16_t tsid = 0;
    uint16_t onid = 0;
    std::string delivery; // human readable delivery system descriptor
    std::vector<uint16_t> serviceIds;
};

struct TBSBBPROC_API NetworkInfo
{
    uint16_t networkId = 0;
    bool actual = true;
    int version = -1;
    std::string name;
    std::vector<TransportInfo> transports;
};

struct TBSBBPROC_API BouquetInfo
{
    uint16_t bouquetId = 0;
    int version = -1;
    std::string name;
    std::vector<TransportInfo> transports;
};

struct TBSBBPROC_API TableStat
{
    uint8_t tableId = 0;
    uint16_t pid = 0;
    std::string name;
    uint64_t sections = 0;
    uint64_t crcErrors = 0;
    int version = -1;     // last version seen (any table id extension)
    uint32_t instances = 0; // distinct table id extensions seen
};

struct TBSBBPROC_API TsReport
{
    bool patSeen = false;
    uint16_t tsid = 0;
    int patVersion = -1;
    uint16_t onid = 0; // from SDT actual
    std::vector<ProgramInfo> programs;     // sorted by program number
    std::vector<ServiceInfo> services;     // SDT actual (+ other), sorted
    std::vector<NetworkInfo> networks;     // NIT actual first
    std::vector<BouquetInfo> bouquets;
    std::vector<CaInfo> emm;               // from the CAT
    bool hasTime = false;
    int64_t utcTime = 0;                   // last TDT/TOT, seconds since 1970
    std::vector<TableStat> tables;         // per (table id, PID)
    std::map<uint16_t, std::string> pidUsage; // what each PID carries
    uint64_t sections = 0;
    uint64_t crcErrors = 0;
    uint64_t scheduleEvents = 0;           // EIT schedule events seen
    uint64_t epgEvents = 0;                // events kept for the EPG (all services)
    uint64_t epgVersion = 0;               // changes whenever the EPG content changes
};

// Content descriptor genre (EN 300 468 table 29) for level 1 << 4 | level 2
TBSBBPROC_API std::string contentGenreName(uint8_t nibbles);

// XMLTV document (https://wiki.xmltv.org) of the given schedules. Channel ids are
// "<service>.<tsid>.<onid>.dvb".
TBSBBPROC_API std::string formatXmltv(const std::vector<ServiceSchedule> &schedules);

// Name tables shared with the UI
TBSBBPROC_API std::string streamTypeName(uint8_t streamType);
TBSBBPROC_API std::string serviceTypeName(uint8_t serviceType);
TBSBBPROC_API std::string caSystemName(uint16_t systemId);
TBSBBPROC_API std::string tableName(uint8_t tableId);
TBSBBPROC_API std::string runningStatusName(uint8_t status);

} // namespace bb
} // namespace tbs
