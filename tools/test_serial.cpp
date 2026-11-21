// test_serial: the debug serial port ($D8003C-$D8003E) and the dev unit that turns it into a UART (docs/debug-protocol.md).
//
//   * retail unit: the port is exactly what it was at revision 10 (TX queue fills and is never drained; nothing reaches a sink;
//     host input is ignored) and the dev-unit latch reads 0
//   * the latch is host-only: no guest write to $D80048 sets or clears it
//   * dev unit: a byte goes out SERIAL_CYCLES_PER_BYTE cycles after it starts, queued bytes follow back to back, the sink
//     sees them in order, and *when the host looks* (every cycle, or once at the end) changes nothing the guest can see
//   * RX: bytes the host sends arrive in order, a full queue drops the byte and sets OVERRUN until cleared
//   * movies: serial input is recorded and replays identically (checkpoints verify), the revision window accepts rev 10 and
//     refuses what it should, and a dev-unit movie round-trips through the binary and text forms
//   * SerialHost: file output, file input, and a TCP client both ways
//   * with the real Boot ROMs (optional 2nd and 3rd arguments: ZPbootROM's bootrom.bin = RETAIL and bootrom_dev.bin = DEV):
//     an unsigned cartridge is handed off to only by the dev Boot ROM on a dev unit; the retail Boot ROM never is, even with
//     the latch set (it doesn't read it), and the dev Boot ROM on a retail unit verifies like the retail one
#include "movie.h"
#include "rom.h"
#include "serial_host.h"
#include "system.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace ZeroPoint;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

constexpr uint32_t SER_STATUS = 0xD8003C, SER_DATA = 0xD8003D, SER_CTRL = 0xD8003E, DEV_MODE = 0xD80048;
constexpr uint8_t RX_READY = 1, TX_EMPTY = 2, OVERRUN = 4, TX_DONE = 8;
constexpr uint64_t N = CPU::SERIAL_CYCLES_PER_BYTE;

// A CPU whose clock the test moves by hand (idleAdvance), with a log of what reached the sink and when.
struct Rig {
    System sys;           // the I/O registers are wired up by System, not by a bare CPU
    CPU& cpu;
    std::vector<std::pair<uint64_t, uint8_t>> out;
    explicit Rig(bool devUnit) : cpu(sys.getCPU()) {
        cpu.reset();
        cpu.setDevUnit(devUnit);
        cpu.setSerialSink([this](uint8_t b) { out.push_back({cpu.getCycleCount(), b}); });
    }
    void tick(uint64_t cycles) { cpu.idleAdvance(cycles, 1); }
    uint8_t status() { return cpu.readByte(SER_STATUS); }
    void put(uint8_t b) { cpu.writeByte(SER_DATA, b); }
};

void testRetail() {
    std::printf("retail unit\n");
    Rig r(false);
    check((r.cpu.readByte(DEV_MODE) & 2) == 0, "dev-unit latch reads 0");
    for (int i = 0; i < 300; i++) r.put((uint8_t)i);
    check((r.status() & TX_EMPTY) == 0, "255 bytes fill the TX queue (the 256th and later are dropped)");
    r.tick(10 * N);
    r.cpu.serialSettle();
    check(r.out.empty(), "nothing is ever sent to a sink");
    check((r.status() & ~TX_EMPTY) == 0, "status has only the rev-10 bits (no OVERRUN / TX_DONE)");
    check(!r.cpu.serialReceive('x') && (r.status() & RX_READY) == 0, "host input is ignored");
    r.cpu.writeByte(SER_CTRL, 1);
    check((r.status() & TX_EMPTY) != 0, "SER_CTRL bit 0 clears the queues");
    r.cpu.writeByte(SER_CTRL, 0x02);
    r.cpu.writeByte(SER_CTRL, 0x04);
    check((r.status() & 0xFC) == 0, "other CTRL bits do nothing on a retail unit");
    r.cpu.writeByte(DEV_MODE, 0xFF);
    check((r.cpu.readByte(DEV_MODE) & 2) == 0 && (r.cpu.readByte(DEV_MODE) & 1) == 1, "a guest write sets DEV_MODE bit 0 but cannot reach the latch");
}

void testLatchIsHostOnly() {
    std::printf("dev-unit latch\n");
    Rig r(true);
    check((r.cpu.readByte(DEV_MODE) & 2) == 2, "reads 1 on a dev unit");
    r.cpu.writeByte(DEV_MODE, 0x00);
    check((r.cpu.readByte(DEV_MODE) & 2) == 2, "writing 0 does not clear it");
    r.cpu.reset();
    check((r.cpu.readByte(DEV_MODE) & 2) == 2, "it survives reset(), like a host setting");
    Rig q(false);
    q.cpu.writeByte(DEV_MODE, 0x02);
    check((q.cpu.readByte(DEV_MODE) & 2) == 0, "writing 1 does not set it on a retail unit");
}

void testTxTiming() {
    std::printf("dev unit TX\n");
    {
        Rig r(true);
        check((r.status() & TX_DONE) != 0 && (r.status() & TX_EMPTY) != 0, "idle: TX_EMPTY and TX_DONE");
        r.put('A');
        check((r.status() & TX_DONE) == 0, "a queued byte clears TX_DONE");
        r.tick(N - 1);
        check(r.out.empty() && (r.status() & TX_DONE) == 0, "not out one cycle early");
        r.tick(1);
        r.cpu.serialSettle();
        check(r.out.size() == 1 && r.out[0].second == 'A', "out after exactly SERIAL_CYCLES_PER_BYTE cycles");
        check((r.status() & TX_DONE) != 0, "TX_DONE again");
    }
    {
        Rig r(true);
        r.put('a'); r.put('b'); r.put('c');
        r.tick(3 * N);
        r.cpu.serialSettle();
        check(r.out.size() == 3 && r.out[0].second == 'a' && r.out[1].second == 'b' && r.out[2].second == 'c', "three bytes, in order");
    }
    {
        // A byte queued while another is going out follows it back to back: done at 2N, not at (write time + N).
        Rig r(true);
        r.put('a');
        r.tick(N / 2);
        r.put('b');
        r.tick(N / 2);                       // 'a' finishes now
        check(r.status() != 0 && r.cpu.getCycleCount() == N, "(clock at N)");
        r.tick(N - 1);
        check(r.out.size() == 1, "'b' is not out before 2N");
        r.tick(1);
        r.cpu.serialSettle();
        check(r.out.size() == 2, "'b' is out at 2N");
    }
    {
        // Idle in between: a byte written after the line went quiet starts from the write, not from the old schedule.
        Rig r(true);
        r.put('a');
        r.tick(5 * N);
        r.put('b');
        r.tick(N - 1);
        r.cpu.serialSettle();
        check(r.out.size() == 1, "after a pause, the next byte takes a full byte time");
        r.tick(1);
        r.cpu.serialSettle();
        check(r.out.size() == 2, "...and then goes out");
    }
    {
        Rig r(true);
        for (int i = 0; i < 300; i++) r.put((uint8_t)i);
        check((r.status() & TX_EMPTY) == 0, "255 queued: TX_EMPTY clear");
        r.tick(N);
        check((r.status() & TX_EMPTY) != 0, "one byte out: room again");
        r.cpu.serialSettle();
        check(r.out.size() == 1 && r.out[0].second == 0, "the first byte written is the first out (overflow dropped the later ones)");
    }
    {
        Rig r(true);
        r.put('x'); r.put('y');
        r.cpu.writeByte(SER_CTRL, 1);
        r.tick(5 * N);
        r.cpu.serialSettle();
        check(r.out.empty(), "SER_CTRL bit 0 discards what has not gone out");
    }
}

void testObservationDoesNotMatter() {
    std::printf("when the host looks\n");
    // Same guest actions, but one rig is settled every cycle and the other only at the end: same guest-visible state, same bytes.
    Rig a(true), b(true);
    const char* msg = "hello, dev unit";
    for (const char* p = msg; *p; p++) {
        for (Rig* r : {&a, &b}) r->put((uint8_t)*p);
        for (uint64_t k = 0; k < 700; k++) { a.tick(1); a.cpu.serialSettle(); (void)a.status(); }
        b.cpu.idleAdvance(700, 700);   // same cycles and same instruction count as a's 700 single steps
    }
    for (uint64_t k = 0; k < 20 * N; k++) { a.tick(1); a.cpu.serialSettle(); }
    b.cpu.idleAdvance(20 * N, 20 * N);
    b.cpu.serialSettle();
    std::string sa, sb;
    for (auto& e : a.out) sa += (char)e.second;
    for (auto& e : b.out) sb += (char)e.second;
    check(sa == msg && sb == msg, "both sinks received the message");
    check(a.cpu.stateHash() == b.cpu.stateHash(), "stateHash is identical");
    // And the hash does depend on the port's state (so a desync would show).
    Rig c(true), d(true);
    c.put('a'); d.put('b');
    check(c.cpu.stateHash() != d.cpu.stateHash(), "stateHash sees queued bytes");
    Rig e1(true), e2(false);
    check(e1.cpu.stateHash() != e2.cpu.stateHash(), "stateHash sees the dev-unit latch");
}

void testRx() {
    std::printf("dev unit RX\n");
    Rig r(true);
    check((r.status() & RX_READY) == 0, "empty: RX_READY clear");
    check(r.cpu.serialReceive('h') && r.cpu.serialReceive('i'), "host bytes accepted");
    check((r.status() & RX_READY) != 0, "RX_READY set");
    check(r.cpu.readByte(SER_DATA) == 'h' && r.cpu.readByte(SER_DATA) == 'i', "read back in order");
    check((r.status() & RX_READY) == 0, "drained");
    int accepted = 0;
    for (int i = 0; i < 300; i++) accepted += r.cpu.serialReceive((uint8_t)i) ? 1 : 0;
    check(accepted == 255, "255 fit in the queue");
    check((r.status() & OVERRUN) != 0, "a dropped byte sets OVERRUN");
    check(!r.cpu.serialRxHasRoom(), "serialRxHasRoom() says so first");
    r.cpu.writeByte(SER_CTRL, 0x02);
    check((r.status() & OVERRUN) == 0 && (r.status() & RX_READY) != 0, "SER_CTRL bit 1 clears OVERRUN and keeps the data");
    r.cpu.writeByte(SER_CTRL, 0x01);
    check((r.status() & RX_READY) == 0 && r.cpu.serialRxHasRoom(), "SER_CTRL bit 0 clears the queues");
}

// A System whose CPU spins in work RAM, so its clock runs.
void spinInRam(System& sys) {
    CPU& cpu = sys.getCPU();
    cpu.writeByte(0x800100, 0x07);
    cpu.writeByte(0x800101, 0x00);
    cpu.writeByte(0x800102, 0x01);
    CPUFlags f = cpu.getFlags();
    f.I = true;
    cpu.setFlags(f);
    cpu.setPB(0x80);
    cpu.setPC(0x0100);
}

void testSystemAndMovies() {
    std::printf("System and movies\n");
    {
        System sys;
        std::string got;
        sys.setSerialSink([&](uint8_t b) { got += (char)b; });
        sys.setDevUnit(true);
        sys.powerOn();
        spinInRam(sys);
        sys.getCPU().writeByte(SER_DATA, 'o');
        sys.getCPU().writeByte(SER_DATA, 'k');
        sys.runCycles(System::CYCLES_PER_FRAME);   // ~1.3 ms of console time: far more than two byte times
        check(got == "ok", "System::runCycles hands finished bytes to the sink");
        check(sys.isDevUnit() && (sys.getCPU().readByte(DEV_MODE) & 2), "System::isDevUnit / latch");
    }
    {
        // A retail System's hash is not changed by merely having a sink attached.
        System a, b;
        b.setSerialSink([](uint8_t) {});
        a.powerOn(); b.powerOn();
        spinInRam(a); spinInRam(b);
        a.runCycles(System::CYCLES_PER_FRAME); b.runCycles(System::CYCLES_PER_FRAME);
        check(a.stateHash() == b.stateHash(), "retail: sink or no sink, same state");
    }
    // Record a dev-unit session with typed input, then replay it with checkpoints.
    Movie rec;
    {
        System sys;
        MovieSession session;
        MovieSession::RecordSetup setup;
        setup.devUnit = true;
        setup.checkpointInterval = System::CYCLES_PER_FRAME * 4;
        std::string err;
        check(session.beginRecording(sys, setup, err), "beginRecording on a dev unit");
        check(sys.isDevUnit(), "the session switched the dev unit on");
        spinInRam(sys);
        session.run(System::CYCLES_PER_FRAME * 8);
        session.serialIn('a');
        session.run(System::CYCLES_PER_FRAME * 3 + 17);
        session.serialIn('b'); session.serialIn('c');
        session.run(System::CYCLES_PER_FRAME * 8);
        rec = session.finishRecording();
        size_t serial = 0;
        for (const MovieEvent& e : rec.events) serial += e.type == MovieEvent::SerialIn;
        check(serial == 3 && rec.devUnit, "3 serial events and the dev-unit flag recorded");
        check(sys.getCPU().readByte(SER_DATA) == 'a', "recorded input reached the guest");
    }
    {
        std::string err;
        std::vector<uint8_t> bytes = rec.serialize();
        Movie back;
        check(Movie::parse(bytes.data(), bytes.size(), back, err) && back.devUnit && back.events.size() == rec.events.size(), "binary round trip");
        Movie viaText;
        check(Movie::fromText(rec.toText(), viaText, err) && viaText.devUnit && viaText.serialize() == bytes, "text round trip");
        check(rec.toText().find("serial 0x61") != std::string::npos && rec.toText().find("devunit on") != std::string::npos, "text form shows them");
    }
    {
        // Replay: the spin loop is set up by the test (a guest's code isn't in a movie), exactly as at record time.
        System sys;
        MovieSession session;
        RomLibrary lib;
        std::string err;
        check(session.beginPlayback(sys, rec, lib, err), "beginPlayback");
        spinInRam(sys);
        while (!session.finished() && !session.desynced() && session.failure().empty()) session.run(System::CYCLES_PER_FRAME);
        check(!session.desynced() && session.failure().empty() && session.checkpointsVerified() > 3, "replay verifies every checkpoint");
        check(sys.getCPU().readByte(SER_DATA) == 'a' && sys.getCPU().readByte(SER_DATA) == 'b' && sys.getCPU().readByte(SER_DATA) == 'c',
              "replay delivered the same bytes");
        // Typed input during playback is ignored (the movie is in control).
        session.serialIn('z');
        check(sys.getCPU().readByte(SER_STATUS) % 2 == 0, "playback ignores live serial input");
    }
    {
        // The revision window: rev 10 movies (made before the dev unit existed) still replay; older and newer do not.
        Movie m;
        m.coreRevision = 10;
        m.lengthCycles = System::CYCLES_PER_FRAME;
        std::string err;
        RomLibrary lib;
        { System s; MovieSession ms; check(ms.beginPlayback(s, m, lib, err), "a revision-10 movie replays"); }
        m.coreRevision = 9;
        { System s; MovieSession ms; check(!ms.beginPlayback(s, m, lib, err), "revision 9 is refused"); }
        m.coreRevision = System::CORE_REVISION + 1;
        { System s; MovieSession ms; check(!ms.beginPlayback(s, m, lib, err), "a newer revision is refused"); }
        Movie d;
        d.coreRevision = 10;
        d.devUnit = true;
        std::vector<uint8_t> bytes = d.serialize();
        Movie back;
        check(!Movie::parse(bytes.data(), bytes.size(), back, err), "a dev-unit movie claiming revision 10 is refused");
    }
}

#ifndef _WIN32
bool waitFor(const std::function<bool()>& cond) {
    for (int i = 0; i < 200; i++) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return cond();
}
#endif

void testHost(const std::string& dir) {
    std::printf("SerialHost\n");
    const std::string outPath = dir + "/serial_out.txt", inPath = dir + "/serial_in.txt";
    {
        SerialHost h;
        std::string err;
        check(h.addOutput(outPath, err), "open an output file");
        for (const char* p = "line one\nline two"; *p; p++) h.send((uint8_t)*p);
    }
    {
        std::ifstream f(outPath, std::ios::binary);
        std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        check(s == "line one\nline two", "output file has the bytes");
    }
    {
        std::ofstream f(inPath, std::ios::binary);
        f << "typed";
    }
    {
        SerialHost h;
        std::string err, got;
        check(h.addInput(inPath, err), "open an input file");
        uint8_t b;
        for (int i = 0; i < 200 && got.size() < 5; i++) {
            while (h.receive(b)) got += (char)b;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check(got == "typed", "input file bytes arrive");
        check(!h.addInput(dir + "/does-not-exist", err) && !err.empty(), "a missing input file is an error");
    }
#ifndef _WIN32
    {
        SerialHost h;
        std::string err;
        const uint16_t port = (uint16_t)(20000 + (::getpid() % 20000));
        if (!h.listenTcp(port, err)) {
            check(false, ("listen: " + err).c_str());
            return;
        }
        int s = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(::connect(s, (sockaddr*)&a, sizeof a) == 0, "connect to 127.0.0.1");
        // The server notices the client in its accept thread; send until the guest's byte arrives.
        char c = 0;
        bool sent = false;
        for (int i = 0; i < 200 && !sent; i++) {
            h.send('G');
            timeval tv{0, 20000};
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            sent = ::recv(s, &c, 1, 0) == 1;
        }
        check(sent && c == 'G', "a guest byte reaches the TCP client");
        check(::send(s, "Q", 1, 0) == 1, "client sends");
        uint8_t b = 0;
        check(waitFor([&]() { return h.receive(b); }) && b == 'Q', "a client byte reaches the guest queue");
        ::close(s);
    }
#endif
}

// Does the image contain the 24-bit operand $D80048 (48 00 D8), i.e. a read of the dev-unit latch (`lda $D80048` is 3D 48 00 D8)?
// Only the dev Boot ROM should.
int latchReads(const std::string& image) {
    std::ifstream f(image, std::ios::binary);
    const std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    int n = 0;
    for (size_t i = 0; i + 2 < b.size(); i++) n += b[i] == 0x48 && b[i + 1] == 0x00 && b[i + 2] == 0xD8;
    return n;
}

// The two real Boot ROMs + an unsigned cartridge ("LDA #$42 / STA $800010 / JMP loop" at $0203, as in test_boot_rom).
void testRealBootRoms(const std::string& retailRom, const std::string& devRom, const std::string& dir) {
    std::printf("real Boot ROMs (retail and dev)\n");
    check(latchReads(retailRom) == 0, "the retail image never reads the dev-unit latch");
    check(latchReads(devRom) >= 1, "the dev image does");
    const uint8_t code[] = {0x37, 0x42, 0x00, 0x55, 0x10, 0x00, 0x80, 0x0F, 0x0A, 0x02};
    std::vector<uint8_t> payload(0x0203, 0x00);
    payload.insert(payload.end(), code, code + sizeof code);
    ZPBHeader header{};
    std::memcpy(header.magic, "ZPB", 4);
    header.version = 1;
    header.headerSize = sizeof(ZPBHeader);
    header.romSize = (uint32_t)payload.size();
    header.entryPoint = 0x0203;
    std::strncpy(header.title, "UNSIGNED", sizeof header.title);
    const std::string path = dir + "/unsigned_cart.zpb";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(&header), sizeof header);
        out.write(reinterpret_cast<const char*>(payload.data()), (std::streamsize)payload.size());
    }
    // Runs the machine until the cartridge has written its marker, the CPU halts, or the budget runs out.
    auto boot = [&](const std::string& rom, bool devUnit, int budget, int& frames, bool& halted) {
        System sys;
        sys.loadBootROM(rom);
        sys.loadROM(path);
        sys.setDevUnit(devUnit);
        sys.powerOn();
        frames = 0;
        for (; frames < budget; frames++) {
            sys.runCycles(System::CYCLES_PER_FRAME);
            if (sys.getCPU().readByte(0x800010) == 0x42 || sys.getCPU().isHalted()) break;
        }
        halted = sys.getCPU().isHalted();
        return sys.getCPU().readByte(0x800010) == 0x42;
    };
    int devFrames = 0, frames = 0;
    bool halted = false;
    const bool devRan = boot(devRom, true, 20000, devFrames, halted);
    std::printf("  (dev Boot ROM on a dev unit: handed off after %d frames = %.2f s of console time)\n", devFrames,
                devFrames * 22185.0 / 17002839.0);
    check(devRan && !halted, "dev Boot ROM + dev unit: an unsigned cartridge is handed off to and runs");
    // Everything else is the RSA verification, tens of seconds of console time on an unsigned ROM: waiting for the halt
    // would make this the slowest test in the suite, so give each 20x the handoff time and require that it has not handed
    // off (it is still verifying) instead.
    check(!boot(devRom, false, devFrames * 20, frames, halted), "dev Boot ROM on a RETAIL unit: verifies, no handoff");
    check(!boot(retailRom, true, devFrames * 20, frames, halted), "retail Boot ROM on a dev unit: verifies anyway (it never reads the latch)");
    check(!boot(retailRom, false, devFrames * 20, frames, halted), "retail Boot ROM on a retail unit: verifies, no handoff");
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    testRetail();
    testLatchIsHostOnly();
    testTxTiming();
    testObservationDoesNotMatter();
    testRx();
    testSystemAndMovies();
    testHost(dir);
    if (argc > 3) testRealBootRoms(argv[2], argv[3], dir);
    if (failures) {
        std::printf("\n%d FAILED\n", failures);
        return 1;
    }
    std::printf("\nall serial checks passed\n");
    return 0;
}
