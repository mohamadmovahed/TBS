#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Small HTTP/1.1 server with WebSocket push, for the daemon's API and dashboard.
// One thread per connection (bounded); not meant to face the internet without a proxy.
namespace http {

struct Request
{
    std::string method;
    std::string path;                          // without the query string
    std::map<std::string, std::string> query;  // decoded parameters
    std::map<std::string, std::string> headers; // lower-case names
    std::string body;
    std::string remote;

    std::string header(const std::string &lowerName) const;
    std::string param(const std::string &name, const std::string &def = {}) const;
};

struct Response
{
    int status = 200;
    std::string contentType = "application/json";
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;

    static Response json(const std::string &body, int status = 200);
    static Response text(const std::string &body, int status = 200, const std::string &type = "text/plain; charset=utf-8");
    static Response error(int status, const std::string &message); // {"error": "..."}
};

class Server
{
public:
    using Handler = std::function<Response(const Request &)>;
    // Decides whether a WebSocket upgrade on wsPath is allowed (authentication)
    using WsAccept = std::function<bool(const Request &)>;
    // A WebSocket client connected (send it the initial state with sendTo)
    using WsOpen = std::function<void(int clientId)>;

    Server();
    ~Server();

    void setHandler(Handler h) { m_handler = std::move(h); }
    void setWebSocket(const std::string &path, WsAccept accept, WsOpen onOpen = nullptr);

    // host "" or "0.0.0.0" = all IPv4 interfaces, "::" = all (dual stack where available)
    bool listen(const std::string &host, uint16_t port, std::string *error);
    uint16_t port() const { return m_port; }
    void stop();

    // Text message to every WebSocket client / to one
    void broadcast(const std::string &text);
    void sendTo(int clientId, const std::string &text);
    size_t webSocketClients() const;

    static std::string sha1Base64(const std::string &data); // for the handshake (exposed for tests)

private:
    struct Conn;
    void acceptLoop();
    void serve(std::shared_ptr<Conn> c);
    bool handleWebSocket(const std::shared_ptr<Conn> &c, const Request &req);

    Handler m_handler;
    std::string m_wsPath;
    WsAccept m_wsAccept;
    WsOpen m_wsOpen;
    intptr_t m_listenFd = -1;
    uint16_t m_port = 0;
    std::atomic<bool> m_running{false};
    std::thread m_acceptThread;
    mutable std::mutex m_mutex;
    std::map<int, std::shared_ptr<Conn>> m_conns; // all open connections
    std::atomic<int> m_nextId{1};
    std::vector<std::thread> m_threads;
};

std::string urlDecode(const std::string &s);

} // namespace http
