// test_exception: the Boot ROMs' exception entries (docs/exceptions.md) with the real Boot ROM images.
//
//   test_exception <retail bootrom.bin> <dev bootrom_dev.bin> <exception_vectors.zpb> <exception_handler.bin>
//
// The cartridge points its vectors at the Boot ROM's entry table and trips a BRK, a COP, a write to ROM, a read of unmapped
// memory and a write to unmapped memory (ZPdevtools/examples/cpu/exception_vectors.asm); the handler image prints one line per
// exception and resumes it (exception_handler.asm).
//
//   * dev Boot ROM on a dev unit: each exception sends a "ZPX? ..." request, the host answers with the handler image, the
//     handler runs, and the program carries on with A, X, Y intact; the faults are reported with their kind and address
//   * nobody answering: it keeps asking (and waits); a reply with a bad CRC is ignored and asked for again
//   * a whole session recorded as a movie replays identically
//   * a retail unit, or the retail image on any unit, just locks (APU told, CPU halted, nothing ever reaches the sink)
//   * faults exist only for the dev Boot ROM on a dev unit
#include "movie.h"
#include "serial_host.h"
#include "system.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

using namespace ZeroPoint;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

std::string retailRom, devRom, cartPath, handlerPath;

constexpr uint32_t DONE = 0x800027, STARTED = 0x800028;

std::vector<std::string> linesOf(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    return out;
}

bool has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

struct Machine {
    System sys;
    std::string captured;                     // everything the guest transmitted
    bool sinkCalled = false;
    Machine(const std::string& rom, bool devUnit, const std::string& cart) {
        sys.loadBootROM(rom);
        sys.loadROM(cart);
        sys.setDevUnit(devUnit);
        sys.setSerialSink([this](uint8_t b) { captured.push_back((char)b); sinkCalled = true; });
        sys.powerOn();
    }
    uint8_t mem(uint32_t a) { return sys.getCPU().readByte(a); }
    uint16_t mem16(uint32_t a) { return (uint16_t)(mem(a) | (mem(a + 1) << 8)); }
};

// What a frontend does each tick: a few host bytes, only while the guest's queue has room.
void feed(System& sys, SerialHost& host) {
    uint8_t b;
    for (int n = 0; n < 16 && sys.serialRxHasRoom() && host.receive(b); n++) sys.serialReceive(b);
}

// Runs until `done` or the frame budget; returns the frames used.
int runFrames(Machine& m, int budget, const std::function<void()>& perFrame, const std::function<bool()>& done) {
    int f = 0;
    for (; f < budget && !done(); f++) {
        m.sys.runCycles(System::CYCLES_PER_FRAME);
        perFrame();
    }
    return f;
}

std::vector<uint8_t> frameFor(const std::vector<uint8_t>& image, bool badCrc) {
    std::vector<uint8_t> f = {'Z', 'P', 'X', 'I', (uint8_t)(image.size() & 0xFF), (uint8_t)(image.size() >> 8)};
    f.insert(f.end(), image.begin(), image.end());
    uint16_t crc = 0xFFFF;
    for (uint8_t b : image) {
        crc ^= (uint16_t)(b << 8);
        for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    if (badCrc) crc ^= 0x0101;
    f.push_back((uint8_t)(crc & 0xFF));
    f.push_back((uint8_t)(crc >> 8));
    return f;
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void testDevUnit() {
    std::printf("dev Boot ROM on a dev unit, the host answers\n");
    Machine m(devRom, true, cartPath);
    SerialHost host;
    std::string err;
    check(host.setExceptionHandler(handlerPath, err), "the handler image loads");
    m.sys.setSerialSink([&](uint8_t b) { m.captured.push_back((char)b); host.send(b); });
    const int frames = runFrames(m, 6000, [&] { feed(m.sys, host); }, [&] { return m.mem(DONE) == 0xFF || m.sys.getCPU().isHalted(); });
    std::printf("  (finished after %d frames = %.2f s of console time)\n", frames, frames * 22185.0 / 17002839.0);
    if (std::getenv("ZP_VERBOSE")) std::printf("--- guest output:\n%s--- (progress %02X, pc %02X:%04X, halted %d)\n", m.captured.c_str(),
                                               m.mem(0x800026), m.sys.getCPU().getPB(), m.sys.getCPU().getPC(), (int)m.sys.getCPU().isHalted());
    check(m.mem(DONE) == 0xFF, "the program ran to its end");
    check(m.mem16(0x800020) == 0x1234 && m.mem16(0x800022) == 0x5678 && m.mem16(0x800024) == 0x9ABC, "A, X and Y came back from the BRK unchanged");
    const std::vector<std::string> lines = linesOf(m.captured);
    std::string requests, reports;
    for (const std::string& l : lines) (l.compare(0, 5, "ZPX? ") == 0 ? requests : reports) += l + "\n";
    check(has(requests, "ZPX? BRK PC=00:") && has(requests, "ZPX? COP PC=00:"), "a BRK and a COP were each requested");
    check(has(requests, "ZPX? ABORT PC=00:") && has(requests, "F=01:008100"), "a write to ROM: ABORT, fault 01 at $00:8100");
    check(has(requests, "F=02:C00000"), "a read of unmapped memory: fault 02 at $C0:0000");
    check(has(requests, "F=03:C00000"), "a write to unmapped memory: fault 03 at $C0:0000");
    check(has(requests, "A=1234 X=5678 Y=9ABC"), "the request carries the registers");
    check(reports == "H30\nH40\nH01\nH02\nH03\n", "the handler ran for each, in order (BRK, COP, three faults)");
    check(!m.sys.getCPU().isHalted() || m.mem(DONE) == 0xFF, "(and the program halted only at its own end)");
}

void testNobodyAnswers() {
    std::printf("nobody answers\n");
    Machine m(devRom, true, cartPath);
    runFrames(m, 6000, [] {}, [&] { return false; });   // ~8 s of console time: boot, the BRK, then silence
    int requests = 0;
    for (const std::string& l : linesOf(m.captured)) requests += l.compare(0, 8, "ZPX? BRK") == 0;
    check(requests >= 3, "it asks again and again (waits forever)");
    check(!m.sys.getCPU().isHalted() && m.mem(DONE) != 0xFF, "and neither gives up nor carries on");
}

void testBadCrc() {
    std::printf("a reply with a bad CRC\n");
    Machine m(devRom, true, cartPath);
    const std::vector<uint8_t> image = readFile(handlerPath);
    std::vector<uint8_t> pending;
    size_t sent = 0;
    int asked = 0, badSent = 0;
    size_t seenLines = 0;
    m.sys.setSerialSink([&](uint8_t b) {
        m.captured.push_back((char)b);
        if (b == '\n') {
            const std::vector<std::string> l = linesOf(m.captured);
            if (l.size() > seenLines && l.back().compare(0, 5, "ZPX? ") == 0) {
                asked++;
                if (asked == 1) { pending = frameFor(image, true); badSent++; }       // the first answer is corrupt,
                else pending = frameFor(image, false);                                 // every later one is fine
                sent = 0;
            }
            seenLines = l.size();
        }
    });
    runFrames(m, 6000, [&] {
        for (int n = 0; n < 16 && sent < pending.size() && m.sys.serialRxHasRoom(); n++) m.sys.serialReceive(pending[sent++]);
    }, [&] { return m.mem(DONE) == 0xFF; });
    check(badSent == 1 && asked >= 2, "the corrupt image was not run: the guest asked again");
    check(m.mem(DONE) == 0xFF, "the good one was, and the program finished");
}

void testLock(const char* label, const std::string& rom, bool devUnit) {
    std::printf("%s\n", label);
    Machine m(rom, devUnit, cartPath);
    const int frames = runFrames(m, 400000, [] {}, [&] { return m.sys.getCPU().isHalted(); });
    std::printf("  (halted after %d frames = %.1f s of console time)\n", frames, frames * 22185.0 / 17002839.0);
    check(m.sys.getCPU().isHalted(), "the machine halted");
    check(m.mem(STARTED) == 0xAA && m.mem(DONE) != 0xFF, "the cartridge ran and tripped the BRK (it is not a failed signature)");
    check(m.mem(0xA01000) == 2, "the APU was told");
    check(m.sys.getCPU().getPB() == 0xE0, "and it is parked in the Boot ROM");
    check(!m.sinkCalled && m.captured.empty(), "nothing was ever sent to the sink");
}

void testFaultGating() {
    std::printf("faults exist only for the dev Boot ROM on a dev unit\n");
    auto aborts = [&](const std::string& rom, bool dev, bool bootRom) {
        System sys;
        if (bootRom) sys.loadBootROM(rom);
        sys.loadROM(cartPath);
        sys.setDevUnit(dev);
        sys.powerOn();
        CPU& cpu = sys.getCPU();
        cpu.setPB(0x00);
        cpu.writeByte(0x008100, 0x42);   // a store to ROM from the cartridge's bank
        const bool rom1 = cpu.isAbortPending();
        return rom1;
    };
    check(aborts(devRom, true, true), "dev Boot ROM + dev unit: a store to ROM raises ABORT");
    check(!aborts(devRom, false, true), "dev Boot ROM + retail unit: no");
    check(!aborts(retailRom, true, true), "retail Boot ROM + dev unit: no");
    check(!aborts("", true, false), "native (HLE) boot + dev unit: no");
    {
        System sys;
        sys.loadBootROM(devRom);
        sys.loadROM(cartPath);
        sys.setDevUnit(true);
        sys.powerOn();
        CPU& cpu = sys.getCPU();
        cpu.writeByte(0x008100, 0x42);   // PB is $E0 here: the Boot ROM's own accesses never fault
        check(!cpu.isAbortPending(), "the Boot ROM's own accesses never fault");
        cpu.setPB(0x00);
        (void)cpu.readByte(0xC00000);
        check(cpu.isAbortPending(), "a read of an unmapped bank does");
    }
}

// A whole session recorded as a movie (the responder's bytes are recorded like any typed byte) replays identically.
void testMovie() {
    std::printf("recorded and replayed\n");
    const std::string dir = cartPath.substr(0, cartPath.find_last_of('/'));
    const std::string romDir = devRom.substr(0, devRom.find_last_of('/'));
    Movie rec;
    std::string recorded;
    {
        System sys;
        MovieSession session;
        MovieSession::RecordSetup setup;
        setup.bootRomPath = devRom;
        setup.cartridgePath = cartPath;
        setup.devUnit = true;
        std::string err;
        check(session.beginRecording(sys, setup, err), err.empty() ? "beginRecording" : err.c_str());
        SerialHost host;
        check(host.setExceptionHandler(handlerPath, err), "responder");
        sys.setSerialSink([&](uint8_t b) { recorded.push_back((char)b); host.send(b); });
        for (int f = 0; f < 1500; f++) {
            uint8_t b;
            for (int n = 0; n < 16 && sys.serialRxHasRoom() && host.receive(b); n++) session.serialIn(b);
            session.run(System::CYCLES_PER_FRAME);
        }
        check(sys.getCPU().readByte(DONE) == 0xFF, "the recorded session finished the program");
        rec = session.finishRecording();
    }
    std::string replayed;
    {
        System sys;
        MovieSession session;
        RomLibrary lib;
        lib.add(dir);
        lib.add(romDir);
        std::string err;
        check(session.beginPlayback(sys, rec, lib, err), err.empty() ? "beginPlayback" : err.c_str());
        sys.setSerialSink([&](uint8_t b) { replayed.push_back((char)b); });
        while (!session.finished() && !session.desynced() && session.failure().empty()) session.run(System::CYCLES_PER_FRAME);
        check(!session.desynced() && session.failure().empty() && session.checkpointsVerified() >= 2, "the replay verifies every checkpoint");
        check(sys.getCPU().readByte(DONE) == 0xFF, "the replay finished the program too");
    }
    check(replayed == recorded && !recorded.empty(), "and the replayed output is byte-identical");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <retail bootrom.bin> <dev bootrom_dev.bin> <exception_vectors.zpb> <exception_handler.bin>\n", argv[0]);
        return 2;
    }
    retailRom = argv[1];
    devRom = argv[2];
    cartPath = argv[3];
    handlerPath = argv[4];
    testFaultGating();
    testDevUnit();
    testNobodyAnswers();
    testBadCrc();
    testMovie();
    testLock("dev Boot ROM on a retail unit (latch clear): locks", devRom, false);
    testLock("retail Boot ROM: locks", retailRom, false);
    testLock("retail Boot ROM on a dev unit: locks", retailRom, true);   // (the signature bypass is the dev image's: this one verifies first)
    if (failures) {
        std::printf("\n%d FAILED\n", failures);
        return 1;
    }
    std::printf("\nall exception checks passed\n");
    return 0;
}
