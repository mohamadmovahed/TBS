#pragma once

#include "Json.h"

#include <tbsbbproc/Processor.h>
#include <tbsdvb/Monitor.h>
#include <tbsdvb/Types.h>

#include <string>

// Everything tbs-daemon runs with; stored as JSON (see packaging/tbs-daemon.json).
struct DaemonConfig
{
    tbs::ReceiverConfig receiver;
    bool processingEnabled = true;          // content analysis (tables, EPG, GSE/MPE, IP outputs)
    tbs::bb::ProcessorConfig processing;
    tbs::MonitorConfig monitor;
    std::string listen = "127.0.0.1:8080";  // API and dashboard
    std::string token;                      // empty = no authentication
    bool autostart = true;
    int restartDelaySec = 10;               // retry after the receiver stopped with an error (0 = never)
    uint32_t pipelineBufferMB = 128;
};

json::Value toJson(const DaemonConfig &c);
// Fields that are missing keep their current value in *out; unknown enum names are errors
bool fromJson(const json::Value &v, DaemonConfig *out, std::string *error);

json::Value toJson(const tbs::TuningParams &t);
bool tuningFromJson(const json::Value &v, tbs::TuningParams *t, std::string *error);
