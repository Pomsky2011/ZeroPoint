#include "serial_host.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  namespace { using sock_t = SOCKET; const sock_t BAD_SOCK = INVALID_SOCKET; inline void closeSock(sock_t s) { closesocket(s); } }
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
  namespace { using sock_t = int; const sock_t BAD_SOCK = -1; inline void closeSock(sock_t s) { ::close(s); } }
#endif

namespace ZeroPoint {

namespace {

// State shared with the reader/accept threads. They hold a shared_ptr to it, so it outlives SerialHost: a thread blocked in
// a read of stdin can't be woken portably, and is left to die with the process.
struct Shared {
    std::mutex lock;
    std::deque<uint8_t> rx;
    sock_t listener = BAD_SOCK;
    sock_t client = BAD_SOCK;
    bool stopping = false;
    std::deque<uint8_t> replies;          // bytes the exception-handler responder wants the guest to see (served before rx)
};

void pushRx(Shared& s, const uint8_t* data, size_t n) {
    std::lock_guard<std::mutex> g(s.lock);
    if (s.rx.size() < (1u << 20)) s.rx.insert(s.rx.end(), data, data + n);   // a runaway sender can't eat all memory
}

}  // namespace

// CRC-16/CCITT-FALSE (poly $1021, init $FFFF, MSB first), the Boot ROM's check.
uint16_t crc16(const std::vector<uint8_t>& data) {
    uint16_t crc = 0xFFFF;
    for (uint8_t b : data) {
        crc ^= (uint16_t)(b << 8);
        for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

struct SerialHost::Impl {
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    std::vector<uint8_t> handlerFrame;   // "ZPXI" + length + image + CRC; empty = no responder
    std::string guestLine;               // the guest's output since its last newline (only kept with a responder)
    std::vector<std::FILE*> outputs;     // stdout is in here too (and never closed)
    std::vector<std::FILE*> owned;       // the ones to fclose
    bool wsaStarted = false;
};

SerialHost::SerialHost() : impl(new Impl) {}

SerialHost::~SerialHost() {
    {
        std::lock_guard<std::mutex> g(impl->shared->lock);
        impl->shared->stopping = true;
        if (impl->shared->listener != BAD_SOCK) {
#ifndef _WIN32
            ::shutdown(impl->shared->listener, SHUT_RDWR);   // wakes the accept thread
#endif
            closeSock(impl->shared->listener);
            impl->shared->listener = BAD_SOCK;
        }
        if (impl->shared->client != BAD_SOCK) {
            closeSock(impl->shared->client);
            impl->shared->client = BAD_SOCK;
        }
    }
    for (std::FILE* f : impl->outputs) std::fflush(f);
    for (std::FILE* f : impl->owned) std::fclose(f);
#ifdef _WIN32
    if (impl->wsaStarted) WSACleanup();
#endif
}

bool SerialHost::addOutput(const std::string& path, std::string& error) {
    if (path == "-") {
        impl->outputs.push_back(stdout);
        return true;
    }
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { error = "cannot open " + path + " for the serial output"; return false; }
    impl->outputs.push_back(f);
    impl->owned.push_back(f);
    return true;
}

bool SerialHost::addInput(const std::string& path, std::string& error) {
    std::shared_ptr<Shared> sh = impl->shared;
    if (path == "-") {
        std::thread([sh]() {
            uint8_t buf[256];
            for (;;) {
                const size_t n = std::fread(buf, 1, sizeof buf, stdin);   // blocks; returns short only at EOF/error
                if (n) pushRx(*sh, buf, n);
                if (n < sizeof buf && (std::feof(stdin) || std::ferror(stdin))) break;
            }
        }).detach();
        return true;
    }
    std::error_code ec;
    const bool fifo = std::filesystem::is_fifo(path, ec);
    if (!fifo && !std::filesystem::exists(path, ec)) { error = "cannot find " + path + " for the serial input"; return false; }
    std::thread([sh, path, fifo]() {
        for (;;) {
            std::FILE* f = std::fopen(path.c_str(), "rb");   // for a FIFO this blocks until a writer opens it
            if (!f) break;
            uint8_t buf[256];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) pushRx(*sh, buf, n);
            std::fclose(f);
            if (!fifo) break;                                // a plain file is read once
            std::lock_guard<std::mutex> g(sh->lock);
            if (sh->stopping) break;
        }
    }).detach();
    return true;
}

bool SerialHost::listenTcp(uint16_t port, std::string& error) {
#ifdef _WIN32
    if (!impl->wsaStarted) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { error = "WSAStartup failed"; return false; }
        impl->wsaStarted = true;
    }
#endif
    const sock_t l = ::socket(AF_INET, SOCK_STREAM, 0);
    if (l == BAD_SOCK) { error = "cannot create a socket"; return false; }
    int yes = 1;
    ::setsockopt(l, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // localhost only, on purpose
    if (::bind(l, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(l, 1) != 0) {
        closeSock(l);
        error = "cannot listen on 127.0.0.1:" + std::to_string(port) + " (is it in use?)";
        return false;
    }
    std::shared_ptr<Shared> sh = impl->shared;
    {
        std::lock_guard<std::mutex> g(sh->lock);
        sh->listener = l;
    }
    std::thread([sh, l]() {
        for (;;) {
            const sock_t c = ::accept(l, nullptr, nullptr);
            if (c == BAD_SOCK) break;   // the listener was closed
#ifdef SO_NOSIGPIPE
            int one = 1;
            ::setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
            // A client that stops reading must not freeze the emulator: sends give up after 50 ms and drop the client.
#ifdef _WIN32
            DWORD ms = 50;
            ::setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms), sizeof ms);
#else
            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 50000;
            ::setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#endif
            {
                std::lock_guard<std::mutex> g(sh->lock);
                if (sh->stopping) { closeSock(c); break; }
                if (sh->client != BAD_SOCK) closeSock(sh->client);   // a new connection replaces the old one
                sh->client = c;
            }
            std::thread([sh, c]() {
                uint8_t buf[256];
                for (;;) {
                    const int n = ::recv(c, reinterpret_cast<char*>(buf), sizeof buf, 0);
                    if (n <= 0) break;
                    pushRx(*sh, buf, (size_t)n);
                }
                std::lock_guard<std::mutex> g(sh->lock);
                if (sh->client == c) { closeSock(c); sh->client = BAD_SOCK; }
            }).detach();
        }
    }).detach();
    return true;
}

bool SerialHost::setExceptionHandler(const std::string& path, std::string& error) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { error = "cannot open " + path + " for the exception handler"; return false; }
    std::vector<uint8_t> image;
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) image.insert(image.end(), buf, buf + n);
    std::fclose(f);
    if (image.empty() || image.size() > 0xFE00) {
        error = path + ": an exception handler image is 1 to 65024 bytes";
        return false;
    }
    std::vector<uint8_t> frame = {'Z', 'P', 'X', 'I', (uint8_t)(image.size() & 0xFF), (uint8_t)(image.size() >> 8)};
    frame.insert(frame.end(), image.begin(), image.end());
    const uint16_t crc = crc16(image);
    frame.push_back((uint8_t)(crc & 0xFF));
    frame.push_back((uint8_t)(crc >> 8));
    impl->handlerFrame = std::move(frame);
    return true;
}

void SerialHost::send(uint8_t byte) {
    if (!impl->handlerFrame.empty()) {
        if (byte == '\n') {
            if (impl->guestLine.compare(0, 5, "ZPX? ") == 0) {
                std::lock_guard<std::mutex> g(impl->shared->lock);
                if (impl->shared->replies.empty())
                    impl->shared->replies.insert(impl->shared->replies.end(), impl->handlerFrame.begin(), impl->handlerFrame.end());
            }
            impl->guestLine.clear();
        } else if (impl->guestLine.size() < 256) {
            impl->guestLine.push_back((char)byte);
        }
    }
    for (std::FILE* f : impl->outputs) {
        std::fputc(byte, f);
        if (byte == '\n') std::fflush(f);
    }
    std::lock_guard<std::mutex> g(impl->shared->lock);
    sock_t c = impl->shared->client;
    if (c == BAD_SOCK) return;
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    if (::send(c, reinterpret_cast<const char*>(&byte), 1, flags) != 1) {
        closeSock(c);
        impl->shared->client = BAD_SOCK;
    }
}

void SerialHost::flush() {
    for (std::FILE* f : impl->outputs) std::fflush(f);
}

bool SerialHost::receive(uint8_t& byte) {
    std::lock_guard<std::mutex> g(impl->shared->lock);
    if (!impl->shared->replies.empty()) {
        byte = impl->shared->replies.front();
        impl->shared->replies.pop_front();
        return true;
    }
    if (impl->shared->rx.empty()) return false;
    byte = impl->shared->rx.front();
    impl->shared->rx.pop_front();
    return true;
}

}  // namespace ZeroPoint
