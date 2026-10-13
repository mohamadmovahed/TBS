#pragma once

#include "DaemonConfig.h"
#include "HttpServer.h"
#include "Json.h"

#include <tbsbbproc/Processor.h>
#include <tbsdvb/Monitor.h>
#include <tbsdvb/Receiver.h>

#include <deque>
#include <memory>
#include <mutex>
#include <string>

// Runs the receiver, content processing and monitoring headless, and serves the API:
//   GET  /api/v1/status, /config, /devices, /streams, /services, /epg, /epg.xml, /ip,
//        /alarms, /history, /history.csv, /log
//   PUT  /api/v1/config   POST /api/v1/start, /stop, /restart, /tune
//   WS   /api/v1/ws       pushes status (1/s), alarm, log and state messages
class Daemon
{
public:
    Daemon(DaemonConfig cfg, std::string configPath);
    ~Daemon();

    bool startServer(std::string *error);
    void stopServer();
    bool startReceiver(std::string *error = nullptr);
    void stopReceiver();
    // Once a second: automatic restart, status push
    void tick();

    http::Response handle(const http::Request &req);
    void log(const std::string &message);

private:
    bool authorized(const http::Request &req) const;
    bool saveConfig(std::string *error) const;
    json::Value statusJson() const;
    json::Value streamsJson();
    json::Value servicesJson(int isi);
    json::Value epgJson(int isi, int serviceId);
    json::Value ipJson(int isi);
    json::Value alarmsJson() const;
    json::Value historyJson(int seconds) const;
    json::Value logJson(size_t lines) const;
    void pushState();

    mutable std::mutex m_mutex;
    DaemonConfig m_cfg;
    std::string m_configPath;
    std::unique_ptr<tbs::Receiver> m_rx;
    std::shared_ptr<tbs::bb::Processor> m_proc;
    tbs::Monitor m_monitor;
    http::Server m_server;
    bool m_wanted = false;              // the receiver should be running
    int64_t m_stoppedAtMs = 0;          // when it stopped on its own (for the restart delay)
    std::string m_lastError;
    std::deque<std::pair<int64_t, std::string>> m_log;
    int64_t m_startedMs = 0;
};
