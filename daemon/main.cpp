// tbs-daemon: headless receiver with a REST/WebSocket API and a web dashboard.
//
//   tbs-daemon --config /etc/tbs-daemon.json
//   tbs-daemon --print-config > tbs-daemon.json     default configuration
//
// See README.md ("Headless daemon") for the API.

#include "Daemon.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

namespace {

std::atomic<bool> g_stop{false};

void onSignal(int)
{
    g_stop = true;
}

void usage(const char *argv0)
{
    std::printf("Usage: %s [--config FILE] [--listen ADDR:PORT] [--token TOKEN] [--no-autostart] [--print-config]\n"
                "  --config FILE     JSON configuration; created with defaults if missing, rewritten by PUT /api/v1/config\n"
                "  --listen          API and dashboard address (default 127.0.0.1:8080; 0.0.0.0:8080 for all interfaces)\n"
                "  --token           require \"Authorization: Bearer TOKEN\" (or ?token=) on the API\n"
                "  --no-autostart    wait for POST /api/v1/start\n"
                "  --print-config    print the configuration (defaults or FILE) and exit\n",
                argv0);
}

} // namespace

int main(int argc, char **argv)
{
    std::string configPath, listen, token;
    bool noAutostart = false, print = false, tokenSet = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--config")
            configPath = next();
        else if (a == "--listen")
            listen = next();
        else if (a == "--token") {
            token = next();
            tokenSet = true;
        } else if (a == "--no-autostart")
            noAutostart = true;
        else if (a == "--print-config")
            print = true;
        else if (a == "--help" || a == "-h") {
            usage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            usage(argv[0]);
            return 2;
        }
    }

    DaemonConfig cfg;
    bool created = false;
    if (!configPath.empty()) {
        std::ifstream f(configPath, std::ios::binary);
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            json::Value v;
            std::string err;
            if (!json::parse(ss.str(), &v, &err) || !fromJson(v, &cfg, &err)) {
                std::fprintf(stderr, "error: %s: %s\n", configPath.c_str(), err.c_str());
                return 1;
            }
        } else if (!print) {
            std::ofstream out(configPath, std::ios::binary);
            if (!out || !(out << toJson(cfg).dump(2) << "\n")) {
                std::fprintf(stderr, "error: cannot read or create %s\n", configPath.c_str());
                return 1;
            }
            created = true;
        }
    }
    if (!listen.empty())
        cfg.listen = listen;
    if (tokenSet)
        cfg.token = token;
    if (noAutostart)
        cfg.autostart = false;
    if (print) {
        std::printf("%s\n", toJson(cfg).dump(2).c_str());
        return 0;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif

    Daemon daemon(cfg, configPath);
    if (created)
        daemon.log("Created " + configPath + " with the default configuration");
    std::string err;
    if (!daemon.startServer(&err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (cfg.autostart)
        daemon.startReceiver();
    else
        daemon.log("Waiting for POST /api/v1/start");

    auto next = std::chrono::steady_clock::now();
    while (!g_stop) {
        next += std::chrono::seconds(1);
        while (!g_stop && std::chrono::steady_clock::now() < next)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!g_stop)
            daemon.tick();
    }
    daemon.log("Shutting down");
    daemon.stopReceiver();
    daemon.stopServer();
    return 0;
}
