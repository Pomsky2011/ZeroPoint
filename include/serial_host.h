#pragma once
// The host end of the debug serial port (docs/debug-protocol.md): where a dev unit's TX bytes go and where its RX bytes
// come from. Kept out of the core library on purpose (it needs threads and sockets; the core links nothing but the
// standard library). A frontend wires it up like this:
//
//     SerialHost host;
//     host.addOutput("-", err); host.addInput("-", err); host.listenTcp(5000, err);
//     system.setSerialSink([&](uint8_t b) { host.send(b); });
//     ... every tick: while (system.serialRxHasRoom() && host.receive(b)) system.serialReceive(b);   (or MovieSession::serialIn)
//
// Everything here is host-side and may be as non-deterministic as it likes: the guest only ever sees what System hands it
// at an exact cycle, and a movie records that, not this.
#include <cstdint>
#include <memory>
#include <string>

namespace ZeroPoint {

class SerialHost {
public:
    SerialHost();
    ~SerialHost();
    SerialHost(const SerialHost&) = delete;
    SerialHost& operator=(const SerialHost&) = delete;

    // Guest -> host. "-" is stdout; anything else is a file (truncated) or an existing FIFO. Any number of outputs; every
    // byte goes to all of them (and to the TCP client, if one is connected).
    bool addOutput(const std::string& path, std::string& error);

    // Host -> guest. "-" is stdin; anything else is a file (read once, then it ends) or a FIFO (reopened when its writer
    // goes away, so `echo hi > fifo` can be repeated). A reader thread fills a queue; receive() never blocks.
    bool addInput(const std::string& path, std::string& error);

    // Both directions over TCP on 127.0.0.1:port (localhost only: the port gives whoever connects the guest's console).
    // One client at a time; a new connection replaces the old one. Not available on every platform (error says so).
    bool listenTcp(uint16_t port, std::string& error);

    // Answers the dev Boot ROM's handler requests (docs/exceptions.md) from this host's end: the guest prints a line starting
    // "ZPX? " when an exception needs a handler image; this replies with `path`'s bytes framed as the Boot ROM wants them
    // ("ZPXI", length, bytes, CRC-16). The reply goes through receive() like any host byte (so a recording logs it). One
    // reply is in flight at a time, so the guest asking again while it is still being fed does not stack frames.
    bool setExceptionHandler(const std::string& path, std::string& error);

    void send(uint8_t byte);        // guest -> host (call from the System sink)
    void flush();                   // push buffered output out (a frontend calls this once per tick)
    bool receive(uint8_t& byte);    // host -> guest; false when nothing is waiting

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace ZeroPoint
