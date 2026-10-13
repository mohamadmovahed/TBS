#include "HttpServer.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace http {

namespace {

constexpr size_t kMaxHeader = 16 * 1024;
constexpr size_t kMaxBody = 8 * 1024 * 1024;
constexpr int kMaxConnections = 64;

#ifdef _WIN32
using SockLen = int;
void closeFd(intptr_t fd) { ::closesocket(static_cast<SOCKET>(fd)); }
void shutdownFd(intptr_t fd) { ::shutdown(static_cast<SOCKET>(fd), SD_BOTH); }
#else
using SockLen = socklen_t;
void closeFd(intptr_t fd) { ::close(static_cast<int>(fd)); }
void shutdownFd(intptr_t fd) { ::shutdown(static_cast<int>(fd), SHUT_RDWR); }
#endif

bool sendAll(intptr_t fd, const char *p, size_t n)
{
    while (n > 0) {
#ifdef _WIN32
        const int w = ::send(static_cast<SOCKET>(fd), p, static_cast<int>(std::min<size_t>(n, 1 << 30)), 0);
#else
        const ssize_t w = ::send(static_cast<int>(fd), p, n, MSG_NOSIGNAL);
#endif
        if (w <= 0)
            return false;
        p += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

long recvSome(intptr_t fd, char *buf, size_t n)
{
#ifdef _WIN32
    return ::recv(static_cast<SOCKET>(fd), buf, static_cast<int>(n), 0);
#else
    return static_cast<long>(::recv(static_cast<int>(fd), buf, n, 0));
#endif
}

void setTimeout(intptr_t fd, int seconds)
{
#ifdef _WIN32
    const DWORD tv = static_cast<DWORD>(seconds * 1000);
    ::setsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv));
#else
    timeval tv{seconds, 0};
    ::setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

std::string lower(std::string s)
{
    for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string &s)
{
    const size_t a = s.find_first_not_of(" \t");
    const size_t b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

const char *reason(int status)
{
    switch (status) {
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default: return "Status";
    }
}

// --- SHA-1 (RFC 3174), for the WebSocket handshake ---
struct Sha1
{
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
    void block(const uint8_t *p)
    {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    std::string digest(const std::string &data)
    {
        std::string m = data;
        const uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
        m += static_cast<char>(0x80);
        while (m.size() % 64 != 56)
            m += '\0';
        for (int i = 7; i >= 0; --i)
            m += static_cast<char>(bits >> (i * 8));
        for (size_t i = 0; i < m.size(); i += 64)
            block(reinterpret_cast<const uint8_t *>(m.data() + i));
        std::string out;
        for (uint32_t v : h)
            for (int i = 3; i >= 0; --i)
                out += static_cast<char>(v >> (i * 8));
        return out;
    }
};

std::string base64(const std::string &in)
{
    static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8) | uint8_t(in[i + 2]);
        out += t[v >> 18];
        out += t[(v >> 12) & 63];
        out += t[(v >> 6) & 63];
        out += t[v & 63];
    }
    if (i + 1 == in.size()) {
        const uint32_t v = uint8_t(in[i]) << 16;
        out += t[v >> 18];
        out += t[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == in.size()) {
        const uint32_t v = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8);
        out += t[v >> 18];
        out += t[(v >> 12) & 63];
        out += t[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

std::string wsFrame(uint8_t opcode, const std::string &payload)
{
    std::string f;
    f += static_cast<char>(0x80 | opcode);
    const size_t n = payload.size();
    if (n < 126) {
        f += static_cast<char>(n);
    } else if (n < 65536) {
        f += static_cast<char>(126);
        f += static_cast<char>(n >> 8);
        f += static_cast<char>(n);
    } else {
        f += static_cast<char>(127);
        for (int i = 7; i >= 0; --i)
            f += static_cast<char>(static_cast<uint64_t>(n) >> (i * 8));
    }
    return f + payload;
}

} // namespace

std::string urlDecode(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1]))
                   && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string Request::header(const std::string &lowerName) const
{
    auto it = headers.find(lowerName);
    return it == headers.end() ? std::string() : it->second;
}

std::string Request::param(const std::string &name, const std::string &def) const
{
    auto it = query.find(name);
    return it == query.end() ? def : it->second;
}

Response Response::json(const std::string &body, int status)
{
    Response r;
    r.status = status;
    r.body = body;
    return r;
}

Response Response::text(const std::string &body, int status, const std::string &type)
{
    Response r;
    r.status = status;
    r.contentType = type;
    r.body = body;
    return r;
}

Response Response::error(int status, const std::string &message)
{
    std::string esc;
    for (char c : message)
        esc += c == '"' ? std::string("\\\"") : c == '\\' ? std::string("\\\\") : std::string(1, c);
    return json("{\"error\":\"" + esc + "\"}\n", status);
}

std::string Server::sha1Base64(const std::string &data)
{
    return base64(Sha1().digest(data));
}

struct Server::Conn
{
    intptr_t fd = -1;
    int id = 0;
    std::string remote;
    std::atomic<bool> webSocket{false};
    std::mutex sendMutex;
    bool send(const std::string &data)
    {
        std::lock_guard<std::mutex> lock(sendMutex);
        return sendAll(fd, data.data(), data.size());
    }
};

Server::Server()
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

Server::~Server()
{
    stop();
}

void Server::setWebSocket(const std::string &path, WsAccept accept, WsOpen onOpen)
{
    m_wsPath = path;
    m_wsAccept = std::move(accept);
    m_wsOpen = std::move(onOpen);
}

bool Server::listen(const std::string &host, uint16_t port, std::string *error)
{
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = host == "::" ? AF_INET6 : AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    addrinfo *res = nullptr;
    const std::string h = host.empty() || host == "0.0.0.0" ? std::string() : host;
    const std::string service = std::to_string(port);
    if (::getaddrinfo(h.empty() ? nullptr : h.c_str(), service.c_str(), &hints, &res) != 0 || !res) {
        if (error)
            *error = "cannot resolve listen address " + host;
        return false;
    }
#ifdef _WIN32
    SOCKET s = ::socket(res->ai_family, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
#else
    int s = ::socket(res->ai_family, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) {
#endif
        ::freeaddrinfo(res);
        if (error)
            *error = "socket() failed";
        return false;
    }
    const int one = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&one), sizeof(one));
    if (res->ai_family == AF_INET6) {
        const int zero = 0;
        ::setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char *>(&zero), sizeof(zero));
    }
    const bool ok = ::bind(s, res->ai_addr, static_cast<SockLen>(res->ai_addrlen)) == 0 && ::listen(s, 32) == 0;
    ::freeaddrinfo(res);
    if (!ok) {
        closeFd(static_cast<intptr_t>(s));
        if (error)
            *error = "cannot listen on " + (host.empty() ? std::string("*") : host) + ":" + service
                     + " (address in use or no permission)";
        return false;
    }
    sockaddr_storage addr;
    SockLen len = sizeof(addr);
    ::getsockname(s, reinterpret_cast<sockaddr *>(&addr), &len);
    m_port = ntohs(addr.ss_family == AF_INET6 ? reinterpret_cast<sockaddr_in6 *>(&addr)->sin6_port
                                              : reinterpret_cast<sockaddr_in *>(&addr)->sin_port);
    m_listenFd = static_cast<intptr_t>(s);
    m_running = true;
    m_acceptThread = std::thread(&Server::acceptLoop, this);
    return true;
}

void Server::stop()
{
    if (!m_running.exchange(false))
        return;
    shutdownFd(m_listenFd);
    closeFd(m_listenFd);
    if (m_acceptThread.joinable())
        m_acceptThread.join();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &kv : m_conns)
            shutdownFd(kv.second->fd);
    }
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        threads.swap(m_threads);
    }
    for (std::thread &t : threads)
        if (t.joinable())
            t.join();
}

void Server::acceptLoop()
{
    while (m_running) {
        sockaddr_storage addr;
        SockLen len = sizeof(addr);
#ifdef _WIN32
        SOCKET s = ::accept(static_cast<SOCKET>(m_listenFd), reinterpret_cast<sockaddr *>(&addr), &len);
        if (s == INVALID_SOCKET) {
#else
        int s = ::accept(static_cast<int>(m_listenFd), reinterpret_cast<sockaddr *>(&addr), &len);
        if (s < 0) {
#endif
            if (!m_running)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        auto c = std::make_shared<Conn>();
        c->fd = static_cast<intptr_t>(s);
        c->id = m_nextId++;
        char host[INET6_ADDRSTRLEN] = "?";
        if (addr.ss_family == AF_INET)
            ::inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in *>(&addr)->sin_addr, host, sizeof(host));
        else if (addr.ss_family == AF_INET6)
            ::inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6 *>(&addr)->sin6_addr, host, sizeof(host));
        c->remote = host;
        const int one = 1;
        ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&one), sizeof(one));

        std::lock_guard<std::mutex> lock(m_mutex);
        if (static_cast<int>(m_conns.size()) >= kMaxConnections) {
            const std::string busy = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            sendAll(c->fd, busy.data(), busy.size());
            closeFd(c->fd);
            continue;
        }
        m_conns[c->id] = c;
        // Finished connection threads are joined here so the list stays short
        m_threads.erase(std::remove_if(m_threads.begin(), m_threads.end(), [](std::thread &t) { return !t.joinable(); }),
                        m_threads.end());
        m_threads.emplace_back(&Server::serve, this, c);
    }
}

void Server::serve(std::shared_ptr<Conn> c)
{
    setTimeout(c->fd, 30);
    std::string buf;
    char chunk[8192];
    bool keepAlive = true;
    while (keepAlive && m_running) {
        // Headers
        size_t headerEnd;
        while ((headerEnd = buf.find("\r\n\r\n")) == std::string::npos) {
            if (buf.size() > kMaxHeader)
                goto done;
            const long n = recvSome(c->fd, chunk, sizeof(chunk));
            if (n <= 0)
                goto done;
            buf.append(chunk, static_cast<size_t>(n));
        }
        {
            Request req;
            req.remote = c->remote;
            const std::string head = buf.substr(0, headerEnd);
            buf.erase(0, headerEnd + 4);
            size_t lineEnd = head.find("\r\n");
            const std::string requestLine = head.substr(0, lineEnd);
            const size_t sp1 = requestLine.find(' ');
            const size_t sp2 = requestLine.rfind(' ');
            if (sp1 == std::string::npos || sp2 == sp1) {
                const Response r = Response::error(400, "bad request line");
                const std::string out = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: "
                                        + std::to_string(r.body.size()) + "\r\nConnection: close\r\n\r\n" + r.body;
                c->send(out);
                break;
            }
            req.method = requestLine.substr(0, sp1);
            std::string target = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
            const std::string version = requestLine.substr(sp2 + 1);
            const size_t q = target.find('?');
            req.path = urlDecode(target.substr(0, q));
            if (q != std::string::npos) {
                const std::string qs = target.substr(q + 1);
                size_t pos = 0;
                while (pos <= qs.size()) {
                    const size_t amp = std::min(qs.find('&', pos), qs.size());
                    const std::string kv = qs.substr(pos, amp - pos);
                    const size_t eq = kv.find('=');
                    if (!kv.empty())
                        req.query[urlDecode(kv.substr(0, eq))] = eq == std::string::npos ? std::string() : urlDecode(kv.substr(eq + 1));
                    pos = amp + 1;
                }
            }
            while (lineEnd != std::string::npos) {
                const size_t next = head.find("\r\n", lineEnd + 2);
                const std::string line = head.substr(lineEnd + 2, next == std::string::npos ? std::string::npos : next - lineEnd - 2);
                const size_t colon = line.find(':');
                if (colon != std::string::npos)
                    req.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
                lineEnd = next;
            }
            // Body
            const std::string cl = req.header("content-length");
            const size_t length = cl.empty() ? 0 : static_cast<size_t>(std::strtoull(cl.c_str(), nullptr, 10));
            if (length > kMaxBody) {
                const std::string out = "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                c->send(out);
                break;
            }
            while (buf.size() < length) {
                const long n = recvSome(c->fd, chunk, sizeof(chunk));
                if (n <= 0)
                    goto done;
                buf.append(chunk, static_cast<size_t>(n));
            }
            req.body = buf.substr(0, length);
            buf.erase(0, length);

            if (!m_wsPath.empty() && req.path == m_wsPath && lower(req.header("upgrade")) == "websocket") {
                handleWebSocket(c, req);
                break;
            }
            keepAlive = version == "HTTP/1.1" && lower(req.header("connection")) != "close";

            Response r;
            try {
                r = m_handler ? m_handler(req) : Response::error(404, "not found");
            } catch (const std::exception &e) {
                r = Response::error(500, e.what());
            }
            std::string out = "HTTP/1.1 " + std::to_string(r.status) + " " + reason(r.status) + "\r\n";
            out += "Content-Type: " + r.contentType + "\r\n";
            out += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
            out += "Cache-Control: no-store\r\n";
            for (const auto &h : r.headers)
                out += h.first + ": " + h.second + "\r\n";
            out += keepAlive ? "Connection: keep-alive\r\n\r\n" : "Connection: close\r\n\r\n";
            if (req.method != "HEAD")
                out += r.body;
            if (!c->send(out))
                break;
        }
    }
done:
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_conns.erase(c->id);
    }
    shutdownFd(c->fd);
    closeFd(c->fd);
    // Detach ourselves from m_threads so stop() does not wait on a finished thread
    std::lock_guard<std::mutex> lock(m_mutex);
    for (std::thread &t : m_threads)
        if (t.get_id() == std::this_thread::get_id()) {
            t.detach();
            break;
        }
}

bool Server::handleWebSocket(const std::shared_ptr<Conn> &c, const Request &req)
{
    const std::string key = req.header("sec-websocket-key");
    if (key.empty() || (m_wsAccept && !m_wsAccept(req))) {
        const std::string out = key.empty() ? "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
                                            : "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        c->send(out);
        return false;
    }
    const std::string accept = sha1Base64(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    if (!c->send("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
                 + accept + "\r\n\r\n"))
        return false;
    c->webSocket = true;
    setTimeout(c->fd, 0);
    if (m_wsOpen)
        m_wsOpen(c->id);

    // Read loop: answer pings, stop on close; client messages are ignored
    std::string buf;
    char chunk[4096];
    while (m_running) {
        const long n = recvSome(c->fd, chunk, sizeof(chunk));
        if (n <= 0)
            break;
        buf.append(chunk, static_cast<size_t>(n));
        while (buf.size() >= 2) {
            const uint8_t b0 = static_cast<uint8_t>(buf[0]);
            const uint8_t b1 = static_cast<uint8_t>(buf[1]);
            size_t len = b1 & 0x7F;
            size_t pos = 2;
            if (len == 126) {
                if (buf.size() < 4)
                    break;
                len = (static_cast<uint8_t>(buf[2]) << 8) | static_cast<uint8_t>(buf[3]);
                pos = 4;
            } else if (len == 127) {
                if (buf.size() < 10)
                    break;
                len = 0;
                for (int i = 2; i < 10; ++i)
                    len = (len << 8) | static_cast<uint8_t>(buf[i]);
                pos = 10;
            }
            if (len > kMaxBody)
                return false;
            const bool masked = b1 & 0x80;
            if (buf.size() < pos + (masked ? 4 : 0) + len)
                break;
            std::string payload = buf.substr(pos + (masked ? 4 : 0), len);
            if (masked)
                for (size_t i = 0; i < payload.size(); ++i)
                    payload[i] = static_cast<char>(payload[i] ^ buf[pos + (i & 3)]);
            buf.erase(0, pos + (masked ? 4 : 0) + len);
            const uint8_t op = b0 & 0x0F;
            if (op == 0x8) {
                c->send(wsFrame(0x8, payload.substr(0, 2)));
                return true;
            }
            if (op == 0x9)
                c->send(wsFrame(0xA, payload));
        }
    }
    return true;
}

void Server::broadcast(const std::string &text)
{
    std::vector<std::shared_ptr<Conn>> targets;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &kv : m_conns)
            if (kv.second->webSocket)
                targets.push_back(kv.second);
    }
    const std::string frame = wsFrame(0x1, text);
    for (auto &c : targets)
        if (!c->send(frame))
            shutdownFd(c->fd); // its read loop ends and removes it
}

void Server::sendTo(int clientId, const std::string &text)
{
    std::shared_ptr<Conn> c;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_conns.find(clientId);
        if (it == m_conns.end())
            return;
        c = it->second;
    }
    if (!c->send(wsFrame(0x1, text)))
        shutdownFd(c->fd);
}

size_t Server::webSocketClients() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t n = 0;
    for (auto &kv : m_conns)
        n += kv.second->webSocket ? 1 : 0;
    return n;
}

} // namespace http
