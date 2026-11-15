// fuzz_determinism: differential fuzzing of everything a guest (or a TAS) can do to the machine.
//
//   fuzz_determinism <workdir> [first-seed] [count] [max-frames]
//   fuzz_determinism <workdir> --seed N        (one seed, verbose)
//   fuzz_determinism <workdir> --golden-write <file> <count> <max-frames>
//   fuzz_determinism <workdir> --golden-check <file>
//
// The golden file holds, per seed, one 64-bit digest of every checkpoint of the reference run. The digests are pure
// integer arithmetic over emulated state, so they must come out identical on every host (CI checks them on all of
// them): that is the cross-platform half of the determinism claim.
//
// Each seed makes a random cartridge (random bytes: every CPU opcode, every addressing mode, wild MMIO stores), random PPU
// and APU programs, and a schedule of host events at exact master cycles: random writes to the I/O block, PPU/APU windows
// and RAM, DMA configs, timer/IRQ enables, PPUCTRL/APUCTRL, program (re)loads, and controller input. That schedule is the
// only input; the machine must be a pure function of it. It is run in several ways that must be indistinguishable:
//
//   reference   runCycles() straight from event to event
//   per-cycle   System::step() one master cycle at a time, memory pre-filled with 0xA5
//   sliced      runCycles() cut into random-sized slices (not aligned to events or frames), memory pre-filled 0x5A
//   vthread     random slices with the video worker thread on
//
// and every System::stateHashParts() after each event and at the end is compared against the reference. Under a
// clang -fsanitize=address,undefined build it also hunts for the UB / out-of-bounds a hostile guest could reach.
#include "system.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

using namespace ZeroPoint;

namespace {

// xorshift64*: pure integer, identical on every host.
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) { next(); next(); }
    uint64_t next() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 0x2545F4914F6CDD1Dull;
    }
    uint32_t below(uint32_t n) { return (uint32_t)(next() % n); }
    uint8_t byte() { return (uint8_t)(next() >> 24); }
};

enum class Kind { Poke, Input, LoadPPU, LoadAPU, Dma, Kick };

struct Event {
    uint64_t cycle;
    Kind kind;
    uint32_t addr = 0;
    uint8_t value = 0;
    uint8_t in[3] = {0, 0, 0};
    std::vector<uint8_t> blob;   // program bytes, or the 9 DMA config bytes
};

struct Scenario {
    std::vector<uint8_t> rom;    // cartridge payload
    uint32_t entry = 0;
    std::vector<Event> events;   // sorted by cycle
    uint64_t endCycle = 0;
};

std::vector<uint8_t> randomBytes(Rng& r, size_t n) {
    std::vector<uint8_t> v(n);
    for (auto& b : v) b = r.byte();
    return v;
}

Scenario makeScenario(uint64_t seed, int maxFrames) {
    Rng r(seed);
    Scenario sc;
    // A whole bank of random code (a short ROM would let the PC run into open bus, which reads $FF = HLT), with the HLT
    // opcode itself taken out so the guest keeps running: the machine is frozen once the CPU halts, which would waste
    // every later event.
    sc.rom = randomBytes(r, 65536);
    for (auto& b : sc.rom)
        if (b == 0xFF) b = r.byte() & 0xFE;
    sc.entry = 0;
    const uint64_t frames = 2 + r.below((uint32_t)maxFrames);
    sc.endCycle = frames * System::CYCLES_PER_FRAME;

    const int n = 10 + (int)r.below(150);
    for (int i = 0; i < n; i++) {
        Event e;
        e.cycle = r.next() % sc.endCycle;
        switch (r.below(16)) {
            case 0: case 1: case 2:   // the I/O block
                e.kind = Kind::Poke; e.addr = 0xD80000 + r.below(0x60); e.value = r.byte(); break;
            case 3: case 4:           // PPU window
                e.kind = Kind::Poke; e.addr = 0xB00000 + r.below(0x10000); e.value = r.byte(); break;
            case 5:                   // APU window
                e.kind = Kind::Poke; e.addr = 0xA00000 + r.below(0x10000); e.value = r.byte(); break;
            case 6:                   // work RAM
                e.kind = Kind::Poke; e.addr = 0x800000 + r.below(0x40000); e.value = r.byte(); break;
            case 7:                   // timers / interrupt controller
                e.kind = Kind::Poke;
                e.addr = 0xD80050 + (r.below(4) == 0 ? 9 : r.below(3));
                e.value = r.byte();
                break;
            case 8:                   // PPUCTRL / APUCTRL
                e.kind = Kind::Poke;
                if (r.below(2)) { e.addr = 0xD80000; e.value = 1 + r.below(2); }
                else { e.addr = 0xD80010; e.value = (uint8_t)(1u << r.below(3)); }
                break;
            case 9: case 10:
                e.kind = Kind::Input; e.in[0] = r.byte(); e.in[1] = r.byte(); e.in[2] = r.byte(); break;
            case 11:
                e.kind = Kind::LoadPPU; e.blob = randomBytes(r, 16 + r.below(2048)); break;
            case 12:
                e.kind = Kind::LoadAPU; e.blob = randomBytes(r, 16 + r.below(2048)); break;
            case 14:                  // restart the CPU at a random address: random code jumps into open bus ($FF = HLT)
                e.kind = Kind::Kick; e.addr = r.below(0x10000); break;
            default: {                // a DMA config, source/target aimed at the places that exist
                e.kind = Kind::Dma;
                e.blob = randomBytes(r, 9);
                static const uint8_t banks[] = {0x00, 0x80, 0x81, 0xA0, 0xB0, 0xBE, 0xD8};
                e.blob[3] = banks[r.below(sizeof banks)];
                e.blob[6] = banks[r.below(sizeof banks)];
                e.blob[7] = (uint8_t)(1 + r.below(4));
                break;
            }
        }
        sc.events.push_back(std::move(e));
    }
    // Stable by cycle: ties keep generation order, so every variant applies them identically.
    for (size_t i = 1; i < sc.events.size(); i++)
        for (size_t j = i; j > 0 && sc.events[j].cycle < sc.events[j - 1].cycle; j--) std::swap(sc.events[j], sc.events[j - 1]);
    return sc;
}

void apply(System& sys, const Event& e) {
    switch (e.kind) {
        case Kind::Poke: sys.getCPU().writeByte(e.addr, e.value); break;
        case Kind::Kick:
            sys.getCPU().reset();
            sys.getCPU().setPC((uint16_t)e.addr);
            break;
        case Kind::Input: sys.getCPU().setPlayerInput(e.in[0], e.in[1], e.in[2]); break;
        case Kind::LoadPPU: sys.loadPPUProgram(e.blob.data(), e.blob.size(), 0, 0); break;
        case Kind::LoadAPU: sys.loadAPUProgram(e.blob.data(), e.blob.size(), 0); break;
        case Kind::Dma:
            sys.getCPU().writeByte(0xD80020, 0);
            for (uint8_t b : e.blob) sys.getCPU().writeByte(0xD80021, b);
            break;
    }
}

enum class Mode { Reference, PerCycle, Sliced, VThread };

// Advance to master cycle `target` (or until the CPU halts, which freezes the whole machine) in the mode's way.
void advanceTo(System& sys, uint64_t target, Mode mode, Rng& cut) {
    while (sys.getMasterCycleCount() < target && !sys.getCPU().isHalted()) {
        const uint64_t left = target - sys.getMasterCycleCount();
        if (mode == Mode::PerCycle) {
            sys.step();
        } else if (mode == Mode::Reference) {
            sys.runCycles(left);
        } else {
            sys.runCycles(std::min<uint64_t>(left, 1 + cut.below(cut.below(4) == 0 ? 400000 : 3000)));
        }
    }
}

using Parts = std::vector<std::pair<const char*, uint64_t>>;

// What the reference run actually exercised, so a fuzzer that mostly halts on its first random byte can't pass quietly.
struct Coverage {
    uint64_t cpuInstr = 0, apuInstr = 0, endMaster = 0, haltedAt = 0;
    bool halted = false, ppuRan = false;
};
Coverage g_cov;          // last reference run
Coverage g_total;        // summed over seeds
uint64_t g_haltedEarly = 0, g_seeds = 0, g_ppuRan = 0;

std::vector<Parts> run(const Scenario& sc, const std::string& romPath, Mode mode, int fill, uint64_t seed) {
    void* storage = ::operator new(sizeof(System), std::align_val_t(alignof(System)));
    if (fill >= 0) std::memset(storage, fill, sizeof(System));
    System* sys = new (storage) System();
    sys->setVerifySignatures(false);
    if (!sys->loadROM(romPath)) { std::fprintf(stderr, "cannot load %s\n", romPath.c_str()); std::exit(2); }
    sys->powerOn();
    if (mode == Mode::VThread) sys->setVideoThread(true);
    Rng cut(seed ^ 0xC0FFEE);

    std::vector<Parts> out;
    for (const Event& e : sc.events) {
        advanceTo(*sys, e.cycle, mode, cut);
        apply(*sys, e);
        out.push_back(sys->stateHashParts());
    }
    advanceTo(*sys, sc.endCycle, mode, cut);
    out.push_back(sys->stateHashParts());
    if (mode == Mode::Reference) {
        g_cov.cpuInstr = sys->getCPU().getInstructionCount();
        g_cov.apuInstr = sys->getAPU().getInstructionCount();
        g_cov.endMaster = sys->getMasterCycleCount();
        g_cov.halted = sys->getCPU().isHalted();
        g_cov.ppuRan = sys->getPPU().getState() == PPUState::Running;
    }

    sys->~System();
    ::operator delete(storage, std::align_val_t(alignof(System)));
    return out;
}

bool writeRom(const Scenario& sc, const std::string& path) {
#pragma pack(push, 1)
    struct { char magic[4]; uint8_t version, reserved; uint16_t headerSize; uint32_t romSize, entry; char title[32]; char dev[16]; } h;
#pragma pack(pop)
    std::memset(&h, 0, sizeof h);
    std::memcpy(h.magic, "ZPB", 4);
    h.version = 1;
    h.headerSize = 64;
    h.romSize = (uint32_t)sc.rom.size();
    h.entry = sc.entry;
    std::strcpy(h.title, "fuzz");
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(&h, sizeof h, 1, f);
    std::fwrite(sc.rom.data(), 1, sc.rom.size(), f);
    std::fclose(f);
    return true;
}

// One digest over every checkpoint of a run.
uint64_t digest(const std::vector<Parts>& run) {
    uint64_t h = 1469598103934665603ull;
    for (const Parts& p : run)
        for (const auto& kv : p)
            for (int i = 0; i < 8; i++) h = (h ^ ((kv.second >> (8 * i)) & 0xFF)) * 1099511628211ull;
    return h;
}

uint64_t goldenDigest(uint64_t seed, int maxFrames, const std::string& work) {
    Scenario sc = makeScenario(seed, maxFrames);
    const std::string rom = work + "/fuzz.zpb";
    if (!writeRom(sc, rom)) { std::fprintf(stderr, "cannot write %s\n", rom.c_str()); std::exit(2); }
    return digest(run(sc, rom, Mode::Reference, -1, seed));
}

// Returns true if every variant matched the reference.
bool fuzzOne(uint64_t seed, int maxFrames, const std::string& work, bool verbose) {
    Scenario sc = makeScenario(seed, maxFrames);
    const std::string rom = work + "/fuzz.zpb";
    if (!writeRom(sc, rom)) { std::fprintf(stderr, "cannot write %s\n", rom.c_str()); std::exit(2); }

    std::vector<Parts> ref = run(sc, rom, Mode::Reference, -1, seed);
    g_seeds++;
    g_total.cpuInstr += g_cov.cpuInstr;
    g_total.apuInstr += g_cov.apuInstr;
    if (g_cov.halted && g_cov.endMaster < sc.endCycle / 4) g_haltedEarly++;
    if (g_cov.ppuRan) g_ppuRan++;
    if (verbose)
        std::printf("seed %llu: reference ran %llu CPU instrs, %llu APU instrs, %s, PPU %s, %zu events over %llu cycles\n",
                    (unsigned long long)seed, (unsigned long long)g_cov.cpuInstr, (unsigned long long)g_cov.apuInstr,
                    g_cov.halted ? "CPU halted" : "CPU running", g_cov.ppuRan ? "running" : "idle", sc.events.size(),
                    (unsigned long long)sc.endCycle);
    struct V { const char* name; Mode mode; int fill; };
    const V variants[] = {{"per-cycle/fill A5", Mode::PerCycle, 0xA5},
                          {"sliced/fill 5A", Mode::Sliced, 0x5A},
                          {"vthread/sliced", Mode::VThread, -1}};
    bool ok = true;
    for (const V& v : variants) {
        std::vector<Parts> got = run(sc, rom, v.mode, v.fill, seed);
        size_t i = 0;
        while (i < ref.size() && i < got.size() && ref[i] == got[i]) i++;
        if (i != ref.size() || got.size() != ref.size()) {
            std::string who;
            if (i < ref.size() && i < got.size())
                for (size_t k = 0; k < ref[i].size(); k++)
                    if (ref[i][k].second != got[i][k].second) who += std::string(who.empty() ? "" : ", ") + ref[i][k].first;
            std::printf("seed %llu: %s DIFFERS at event %zu/%zu (%s) cycle %llu: %s\n", (unsigned long long)seed, v.name, i,
                        sc.events.size(), i < sc.events.size() ? "event" : "end",
                        (unsigned long long)(i < sc.events.size() ? sc.events[i].cycle : sc.endCycle), who.c_str());
            ok = false;
        } else if (verbose) {
            std::printf("seed %llu: %-18s identical over %zu checkpoints\n", (unsigned long long)seed, v.name, ref.size());
        }
    }
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <workdir> [first-seed] [count] [max-frames] | --seed N\n", argv[0]);
        return 2;
    }
    const std::string work = argv[1];
    if (argc >= 4 && !std::strcmp(argv[2], "--golden-write")) {
        const uint64_t count = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : 24;
        const int frames = argc > 5 ? std::atoi(argv[5]) : 4;
        FILE* f = std::fopen(argv[3], "w");
        if (!f) { std::fprintf(stderr, "cannot write %s\n", argv[3]); return 2; }
        std::fprintf(f, "# fuzz_determinism golden digests: seed digest   (max-frames %d)\n", frames);
        for (uint64_t s = 1; s <= count; s++)
            std::fprintf(f, "%llu %016llx\n", (unsigned long long)s, (unsigned long long)goldenDigest(s, frames, work));
        std::fclose(f);
        std::printf("wrote %llu digests to %s\n", (unsigned long long)count, argv[3]);
        return 0;
    }
    if (argc >= 4 && !std::strcmp(argv[2], "--golden-check")) {
        FILE* f = std::fopen(argv[3], "r");
        if (!f) { std::fprintf(stderr, "cannot read %s\n", argv[3]); return 2; }
        int frames = 4;
        char line[256];
        uint64_t n = 0, bad = 0;
        while (std::fgets(line, sizeof line, f)) {
            if (line[0] == '#') {
                const char* p = std::strstr(line, "max-frames ");
                if (p) frames = std::atoi(p + 11);
                continue;
            }
            unsigned long long seed = 0, want = 0;
            if (std::sscanf(line, "%llu %llx", &seed, &want) != 2) continue;
            const uint64_t got = goldenDigest(seed, frames, work);
            n++;
            if (got != want) {
                bad++;
                std::printf("seed %llu: digest %016llx, golden %016llx\n", seed, (unsigned long long)got, want);
            }
        }
        std::fclose(f);
        std::printf("golden check: %llu seeds, %s\n", (unsigned long long)n, bad ? "MISMATCH" : "all match");
        return bad || !n ? 1 : 0;
    }
    uint64_t first = 1, count = 100;
    int maxFrames = 8;
    bool verbose = false;
    if (argc > 3 && !std::strcmp(argv[2], "--seed")) {
        first = std::strtoull(argv[3], nullptr, 10);
        count = 1;
        verbose = true;
    } else {
        if (argc > 2) first = std::strtoull(argv[2], nullptr, 10);
        if (argc > 3) count = std::strtoull(argv[3], nullptr, 10);
        if (argc > 4) maxFrames = std::atoi(argv[4]);
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    uint64_t bad = 0;
    for (uint64_t s = first; s < first + count; s++) {
        if (!verbose && (s - first) % 25 == 0) std::fprintf(stderr, "[seed %llu]\n", (unsigned long long)s);
        if (!fuzzOne(s, maxFrames, work, verbose)) bad++;
    }
    std::printf("coverage: avg %llu CPU instrs and %llu APU instrs per seed; CPU halted in the first quarter in %llu/%llu; PPU "
                "still running at the end in %llu/%llu\n",
                (unsigned long long)(g_total.cpuInstr / (g_seeds ? g_seeds : 1)),
                (unsigned long long)(g_total.apuInstr / (g_seeds ? g_seeds : 1)), (unsigned long long)g_haltedEarly,
                (unsigned long long)g_seeds, (unsigned long long)g_ppuRan, (unsigned long long)g_seeds);
    std::printf("%llu seeds from %llu: %s\n", (unsigned long long)count, (unsigned long long)first,
                bad ? "DIVERGED" : "all identical");
    return bad ? 1 : 0;
}
