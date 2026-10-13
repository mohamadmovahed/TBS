// tbs-daemon pieces: JSON, configuration mapping, HTTP/WebSocket server and the API.

#include "Daemon.h"
#include "DaemonConfig.h"
#include "HttpServer.h"
#include "Json.h"

#include "StreamBuilders.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

static int g_failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                              \
        }                                                                              \
    } while (0)

static void testJson()
{
    json::Value v;
    std::string err;
    CHECK(json::parse(R"({"a": [1, 2.5, -3e2, true, null, "x\"yé😀"], "b": {"c": {}}})", &v, &err));
    CHECK(v.get("a").size() == 6);
    CHECK(v.get("a")[1].toNumber() == 2.5);
    CHECK(v.get("a")[2].toNumber() == -300);
    CHECK(v.get("a")[3].toBool());
    CHECK(v.get("a")[4].isNull());
    CHECK(v.get("a")[5].toString() == "x\"y\xc3\xa9\xf0\x9f\x98\x80");
    CHECK(v.get("b").get("c").isObject());
    json::Value back;
    CHECK(json::parse(v.dump(), &back));
    CHECK(back.dump() == v.dump());
    CHECK(json::parse(v.dump(2), &back) && back.dump() == v.dump());
    for (const char *bad : {"", "{", "[1,]", "{\"a\" 1}", "tru", "\"abc", "[1] x", "{\"a\":\"\x01\"}"})
        CHECK(!json::parse(bad, &back, &err));
    json::Value o;
    o["freq"] = 11494.123;
    o["count"] = 12345678901ULL;
    CHECK(o.dump() == R"({"count":12345678901,"freq":11494.123})");
    // Copies are independent (copy on write)
    json::Value a = json::Value::array();
    a.push(1);
    json::Value b = a;
    b.push(2);
    CHECK(a.size() == 1 && b.size() == 2);
}

static void testConfig()
{
    DaemonConfig c;
    c.receiver.device.kind = tbs::DeviceKind::FileReplay;
    c.receiver.device.replayFile = "x.ts";
    c.receiver.tuning.frequencyMHz = 10971.5;
    c.receiver.tuning.polarization = tbs::Polarization::VerticalRight13V;
    c.receiver.capture = tbs::CaptureMode::BBFrame;
    tbs::TsNetworkOutput o;
    tbs::parseNetworkUrl("srt://:9000?mode=listener&program=5", &o);
    o.enabled = false;
    c.receiver.network.push_back(o);
    c.processing.pcap.format = tbs::bb::PcapFormat::PcapNg;
    c.monitor.lowCnr = true;
    c.monitor.cnrMinDb = 4.5;
    c.token = "t";
    const std::string text = toJson(c).dump(2);

    json::Value v;
    CHECK(json::parse(text, &v));
    DaemonConfig d;
    std::string err;
    CHECK(fromJson(v, &d, &err));
    CHECK(toJson(d).dump() == toJson(c).dump());
    CHECK(d.receiver.network.size() == 1 && !d.receiver.network[0].enabled && d.receiver.network[0].programNumber == 5);
    CHECK(d.receiver.tuning.frequencyMHz == 10971.5);

    // Partial updates keep the other values; bad values are rejected with a message
    json::Value patch;
    CHECK(json::parse(R"({"receiver": {"tuning": {"symbolRateKsps": 30000}}, "monitor": {"cnrMinDb": 7}})", &patch));
    CHECK(fromJson(patch, &d, &err));
    CHECK(d.receiver.tuning.symbolRateKsps == 30000 && d.receiver.tuning.frequencyMHz == 10971.5 && d.monitor.cnrMinDb == 7);
    for (const char *bad : {R"({"receiver": {"capture": "x"}})", R"({"receiver": {"network": ["ftp://a:1"]}})",
                            R"({"monitor": {"noLock": 3}})", R"({"receiver": {"tuning": {"symbolRateKsps": 0}}})",
                            R"({"api": 5})"}) {
        CHECK(json::parse(bad, &patch));
        err.clear();
        const DaemonConfig before = d;
        CHECK(!fromJson(patch, &d, &err));
        CHECK(!err.empty());
        CHECK(toJson(d).dump() == toJson(before).dump()); // unchanged on error
    }
}

// --- raw socket client ---
struct Client
{
#ifdef _WIN32
    SOCKET fd;
#else
    int fd;
#endif
    explicit Client(uint16_t port)
    {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a;
        std::memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a));
#ifdef _WIN32
        DWORD tv = 3000;
#else
        timeval tv{3, 0};
#endif
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv));
    }
    ~Client()
    {
#ifdef _WIN32
        ::closesocket(fd);
#else
        ::close(fd);
#endif
    }
    void send(const std::string &s) { ::send(fd, s.data(), static_cast<int>(s.size()), 0); }
    // Reads one HTTP response (headers + Content-Length body)
    std::string response()
    {
        std::string buf;
        char c[4096];
        size_t end;
        while ((end = buf.find("\r\n\r\n")) == std::string::npos) {
            const auto n = ::recv(fd, c, sizeof(c), 0);
            if (n <= 0)
                return buf;
            buf.append(c, static_cast<size_t>(n));
        }
        size_t len = 0;
        const size_t cl = buf.find("Content-Length: ");
        if (cl != std::string::npos && cl < end)
            len = std::strtoul(buf.c_str() + cl + 16, nullptr, 10);
        while (buf.size() < end + 4 + len) {
            const auto n = ::recv(fd, c, sizeof(c), 0);
            if (n <= 0)
                break;
            buf.append(c, static_cast<size_t>(n));
        }
        return buf;
    }
    std::string readSome()
    {
        char c[4096];
        const auto n = ::recv(fd, c, sizeof(c), 0);
        return n > 0 ? std::string(c, static_cast<size_t>(n)) : std::string();
    }
};

static void testHttpServer()
{
    CHECK(http::Server::sha1Base64("dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11")
          == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="); // RFC 6455 example
    http::Server server;
    server.setHandler([](const http::Request &r) {
        if (r.path == "/echo")
            return http::Response::text(r.method + " " + r.param("q") + " " + r.body);
        return http::Response::error(404, "not found");
    });
    int opened = 0;
    server.setWebSocket("/ws", [](const http::Request &r) { return r.param("token") == "ok"; },
                        [&](int id) {
                            ++opened;
                            server.sendTo(id, "hello");
                        });
    std::string err;
    CHECK(server.listen("127.0.0.1", 0, &err));
    CHECK(server.port() != 0);
    {
        Client c(server.port());
        // Two requests on one keep-alive connection
        c.send("GET /echo?q=a%20b HTTP/1.1\r\nHost: x\r\n\r\n");
        const std::string r1 = c.response();
        CHECK(r1.rfind("HTTP/1.1 200 OK", 0) == 0);
        CHECK(r1.find("\r\n\r\nGET a b ") != std::string::npos);
        c.send("POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nhello");
        const std::string r2 = c.response();
        CHECK(r2.find("POST  hello") != std::string::npos);
        c.send("GET /nope HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
        const std::string r3 = c.response();
        CHECK(r3.rfind("HTTP/1.1 404", 0) == 0);
        CHECK(r3.find("Connection: close") != std::string::npos);
    }
    {
        Client c(server.port());
        c.send("GET /ws?token=bad HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: "
               "dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n");
        CHECK(c.response().rfind("HTTP/1.1 401", 0) == 0);
    }
    {
        Client c(server.port());
        c.send("GET /ws?token=ok HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: "
               "dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n");
        std::string r = c.response();
        CHECK(r.rfind("HTTP/1.1 101", 0) == 0);
        CHECK(r.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
        // The greeting from the open callback, then a broadcast
        std::string frames = r.substr(r.find("\r\n\r\n") + 4);
        for (int i = 0; i < 20 && frames.size() < 7; ++i)
            frames += c.readSome();
        CHECK(frames.size() >= 7 && static_cast<uint8_t>(frames[0]) == 0x81 && frames[1] == 5
              && frames.substr(2, 5) == "hello");
        for (int i = 0; i < 50 && server.webSocketClients() == 0; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(server.webSocketClients() == 1);
        const std::string big(300, 'z');
        server.broadcast(big);
        std::string got;
        for (int i = 0; i < 20 && got.size() < 304; ++i)
            got += c.readSome();
        CHECK(got.size() == 304 && static_cast<uint8_t>(got[0]) == 0x81 && static_cast<uint8_t>(got[1]) == 126
              && got.substr(4) == big);
        // Masked close from the client
        const unsigned char close[] = {0x88, 0x82, 1, 2, 3, 4, 0x03 ^ 1, 0xE8 ^ 2};
        c.send(std::string(reinterpret_cast<const char *>(close), sizeof(close)));
        const std::string reply = c.readSome();
        CHECK(!reply.empty() && static_cast<uint8_t>(reply[0]) == 0x88);
    }
    CHECK(opened == 1);
    for (int i = 0; i < 50 && server.webSocketClients() != 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(server.webSocketClients() == 0);
    server.stop();
}

static void testDaemonApi()
{
    // A short BBFrame capture to replay
    std::vector<builders::Bytes> ts;
    for (int i = 0; i < 3000; ++i)
        ts.push_back(builders::makeTsPacket(0x100, static_cast<uint8_t>(i), static_cast<uint8_t>(i)));
    const auto frames = builders::tsToBbFrames(ts, 4000);
    const std::string file = "tbs_test_daemon.ts";
    {
        std::FILE *f = std::fopen(file.c_str(), "wb");
        uint8_t cc = 0;
        for (const auto &fr : frames)
            for (const auto &p : builders::bbFrameToPseudoTs(fr, cc))
                std::fwrite(p.data(), 1, 188, f);
        std::fclose(f);
    }
    DaemonConfig cfg;
    cfg.receiver.device.kind = tbs::DeviceKind::FileReplay;
    cfg.receiver.device.replayFile = file;
    cfg.receiver.device.replayRateMbps = 5;
    cfg.receiver.device.replayLoop = true;
    cfg.receiver.capture = tbs::CaptureMode::BBFrame;
    cfg.token = "tok";
    cfg.restartDelaySec = 0;
    Daemon d(cfg, "");
    auto call = [&](const std::string &method, const std::string &path, const std::string &body = {},
                    bool auth = true) {
        http::Request r;
        r.method = method;
        const size_t q = path.find('?');
        r.path = path.substr(0, q);
        if (q != std::string::npos) {
            const std::string kv = path.substr(q + 1);
            r.query[kv.substr(0, kv.find('='))] = kv.substr(kv.find('=') + 1);
        }
        if (auth)
            r.headers["authorization"] = "Bearer tok";
        r.body = body;
        return d.handle(r);
    };
    CHECK(call("GET", "/api/v1/status", {}, false).status == 401);
    CHECK(call("GET", "/").status == 200);
    CHECK(call("GET", "/api/v1/nothing").status == 404);
    CHECK(call("DELETE", "/api/v1/status").status == 405);

    CHECK(call("POST", "/api/v1/start").status == 200);
    json::Value st;
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        json::parse(call("GET", "/api/v1/status").body, &st);
        if (st.get("state").toString() == "running" && st.get("stats").get("bbFrames").toNumber() > 0)
            break;
    }
    CHECK(st.get("state").toString() == "running");
    CHECK(st.get("frontend").get("locked").toBool());
    CHECK(st.get("stats").get("bbFrames").toNumber() > 0);

    json::Value streams;
    CHECK(json::parse(call("GET", "/api/v1/streams").body, &streams));
    CHECK(streams.size() == 1 && streams[0].get("content").toString() == "Transport stream");

    // Retune is applied and kept; bad tuning is rejected
    const http::Response t = call("POST", "/api/v1/tune", R"({"frequencyMHz": 12100.5})");
    CHECK(t.status == 200 && t.body.find("12100.500 MHz") != std::string::npos);
    CHECK(call("POST", "/api/v1/tune", "{bad").status == 400);
    CHECK(call("POST", "/api/v1/tune", R"({"polarization": "x"})").status == 400);

    json::Value c;
    CHECK(json::parse(call("GET", "/api/v1/config").body, &c));
    CHECK(c.get("api").get("token").toString() == "***");
    CHECK(c.get("receiver").get("tuning").get("frequencyMHz").toNumber() == 12100.5);
    CHECK(call("PUT", "/api/v1/config", R"({"monitor": {"windowSec": 5}})").status == 200);
    CHECK(call("PUT", "/api/v1/config", R"({"receiver": {"capture": "nope"}})").status == 400);

    json::Value alarms;
    CHECK(json::parse(call("GET", "/api/v1/alarms").body, &alarms));
    CHECK(alarms.get("alarms").size() == static_cast<size_t>(tbs::AlarmType::Count));
    const http::Response csv = call("GET", "/api/v1/history.csv");
    CHECK(csv.contentType.find("text/csv") == 0 && csv.body.rfind("time,unix_ms", 0) == 0);
    CHECK(call("GET", "/api/v1/log?lines=5").status == 200);

    // Scans: the frontend scans need the receiver stopped, the network scan needs it running
    auto waitScan = [&]() {
        json::Value sc;
        for (int i = 0; i < 200; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            json::parse(call("GET", "/api/v1/scan").body, &sc);
            if (!sc.get("running").toBool())
                break;
        }
        return sc;
    };
    CHECK(call("POST", "/api/v1/scan/spectrum", R"({"start": 10700, "end": 11700})").status == 409);
    CHECK(call("POST", "/api/v1/scan/network", R"({"tableTimeoutMs": 300, "lockTimeoutMs": 3000})").status == 202);
    CHECK(call("POST", "/api/v1/scan/network", "{}").status == 409); // one at a time
    json::Value sc = waitScan();
    CHECK(sc.get("kind").toString() == "network");
    CHECK(!sc.get("running").toBool());
    CHECK(sc.get("error").toString().empty());
    CHECK(sc.get("result").get("transponders").size() == 1);
    CHECK(sc.get("result").get("transponders")[0].get("locked").toBool());
    CHECK(!sc.get("result").get("transponders")[0].get("tablesComplete").toBool()); // no SI in this stream
    json::Value iq;
    CHECK(json::parse(call("GET", "/api/v1/constellation?samples=256").body, &iq));
    CHECK(iq.get("running").toBool());
    for (int i = 0; i < 40 && iq.get("supported").toBool(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        json::parse(call("GET", "/api/v1/constellation").body, &iq);
    }
    CHECK(!iq.get("supported").toBool()); // file replay has no IQ samples

    CHECK(call("POST", "/api/v1/stop").status == 200);
    json::parse(call("GET", "/api/v1/status").body, &st);
    CHECK(st.get("state").toString() == "stopped" && !st.get("wanted").toBool());

    CHECK(call("POST", "/api/v1/scan/network", "{}").status == 409);
    CHECK(call("POST", "/api/v1/scan/blind", R"({"start": 12000, "end": 11000})").status == 400);
    CHECK(call("POST", "/api/v1/scan/spectrum", R"({"start": 10700, "end": 11700, "polarization": "both"})").status == 202);
    sc = waitScan();
    CHECK(sc.get("kind").toString() == "spectrum");
    CHECK(sc.get("error").toString().find("neumoDVB") != std::string::npos);
    json::Value caps;
    CHECK(json::parse(call("GET", "/api/v1/scan/capabilities").body, &caps));
    CHECK(!caps.get("probed").toBool() && !caps.get("note").toString().empty());
    std::remove(file.c_str());
}

int main()
{
    testJson();
    testConfig();
    testHttpServer();
    testDaemonApi();
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All daemon tests passed\n");
    return 0;
}
