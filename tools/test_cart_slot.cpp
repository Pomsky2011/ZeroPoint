// test_cart_slot: the cartridge slot and the reset/controller hardware that movies drive (docs/movies.md).
//
//   test_cart_slot <v1.zpb> <v3.zpb> <unsigned.zpb>      (tools/test_cart_slot.sh builds the signed ones with zpbuild)
//
//   * port 2 is a controller ($D80038-$D8003A), unplugged until set; the debug serial port moved to $D8003C-$D8003E
//   * hardReset() is a power cycle: after a dirty run it is state-identical to a fresh System, part by part
//   * a yank empties the slot: banks $00-$7F and $E2 read CA/FE, nothing else changes, the CPU keeps running
//   * a Stop 'n' Swop replaces the cartridge in one instant: no reset, no boot, no verify; gating restarts
//   * softReset() on an empty slot is harmless; a bad image is refused and leaves the slot alone
#include "system.h"
#include <cstdio>
#include <fstream>
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

std::vector<uint8_t> slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool sameParts(const std::vector<std::pair<const char*, uint64_t>>& a, const std::vector<std::pair<const char*, uint64_t>>& b,
               std::string* diff = nullptr) {
    bool same = a.size() == b.size();
    for (size_t i = 0; same && i < a.size(); i++) {
        if (a[i].second != b[i].second) {
            same = false;
            if (diff) *diff = a[i].first;
        }
    }
    return same;
}

// Park the CPU in a loop in work RAM ("bra $0100" at $80:0100) so it keeps executing whatever the slot holds.
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
    if (cpu.isHalted()) cpu.reset(), cpu.setFlags(f), cpu.setPB(0x80), cpu.setPC(0x0100);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <v1.zpb> <v3.zpb> <unsigned.zpb>\n", argv[0]);
        return 2;
    }
    const std::vector<uint8_t> v1 = slurp(argv[1]), v3 = slurp(argv[2]), bare = slurp(argv[3]);
    if (v1.empty() || v3.empty() || bare.empty()) { std::fprintf(stderr, "cannot read ROMs\n"); return 2; }

    std::printf("port 2 controller\n");
    {
        System sys;
        CPU& cpu = sys.getCPU();
        check(cpu.readByte(0xD80038) == 0 && cpu.readByte(0xD80039) == 0 && cpu.readByte(0xD8003A) == 0, "unplugged at power-on (all zero)");
        check((cpu.readByte(0xD80031) & PlayerInput::CTRL_CONNECTION) != 0, "port 1 is still plugged in by default");
        cpu.setPlayer2Input(PlayerInput::DIR_UP, PlayerInput::CTRL_CONNECTION | PlayerInput::CTRL_PAUSE, PlayerInput::BTN_1);
        check(cpu.readByte(0xD80038) == PlayerInput::DIR_UP && cpu.readByte(0xD80039) == (PlayerInput::CTRL_CONNECTION | PlayerInput::CTRL_PAUSE) &&
                  cpu.readByte(0xD8003A) == PlayerInput::BTN_1, "setPlayer2Input shows up at $D80038-$D8003A");
        check(cpu.readByte(0xD80030) == 0 && cpu.readByte(0xD80032) == 0, "...without touching port 1");
        cpu.writeByte(0xD80038, 0xFF);
        check(cpu.readByte(0xD80038) == PlayerInput::DIR_UP, "the controller registers are read-only");
        check(cpu.readByte(0xD8003C) == 0x02, "debug serial status moved to $D8003C (TX empty)");
        cpu.writeByte(0xD8003D, 0x42);
        check(cpu.readByte(0xD80038) == PlayerInput::DIR_UP, "serial data at $D8003D does not alias the controller");
    }

    std::printf("hard reset = power cycle\n");
    {
        System fresh;
        fresh.setVerifySignatures(true);
        fresh.insertCartridge(v1);
        fresh.softReset();
        const auto freshParts = fresh.stateHashParts();

        System dirty;
        dirty.insertCartridge(v1);
        dirty.softReset();
        dirty.runCycles(4 * System::CYCLES_PER_FRAME);
        CPU& cpu = dirty.getCPU();
        for (uint32_t i = 0; i < 4096; i++) {
            cpu.writeByte(0x800000 + i * 17, (uint8_t)(i * 7 + 1));    // work RAM
            cpu.writeByte(0xB00200 + (i & 0x3FF), (uint8_t)(i + 3));   // PPU memory
            cpu.writeByte(0xA00100 + (i & 0xFFF), (uint8_t)(i * 3));   // APU memory
        }
        cpu.writeByte(0xD80020, 0);                                    // half a DMA config
        cpu.writeByte(0xD80021, 0x40);
        cpu.writeByte(0xD80050, 0xFF);                                 // every timer on
        cpu.writeByte(0xD80052, 0xFF);
        cpu.setPlayerInput(PlayerInput::DIR_LEFT, PlayerInput::CTRL_CONNECTION, PlayerInput::BTN_2);
        dirty.runCycles(6 * System::CYCLES_PER_FRAME);
        std::string diff;
        check(!sameParts(freshParts, dirty.stateHashParts()), "(the dirty machine really differs from a fresh one)");

        dirty.hardReset();
        // Controller state is the host's, not the console's: a power cycle doesn't unplug or release anything.
        cpu.setPlayerInput(0, PlayerInput::CTRL_CONNECTION, 0);
        const bool same = sameParts(freshParts, dirty.stateHashParts(), &diff);
        check(same, same ? "after hardReset() every part equals a fresh System's" : ("hardReset() left state behind in: " + diff).c_str());
        dirty.runCycles(3 * System::CYCLES_PER_FRAME);
        fresh.runCycles(3 * System::CYCLES_PER_FRAME);
        const bool same2 = sameParts(fresh.stateHashParts(), dirty.stateHashParts(), &diff);
        check(same2, same2 ? "...and they stay identical when run" : ("...but they diverge when run, first in: " + diff).c_str());
    }

    std::printf("yank\n");
    {
        System sys;
        sys.setVerifySignatures(false);
        sys.insertCartridge(v3);
        sys.softReset();
        CPU& cpu = sys.getCPU();
        spinInRam(sys);
        sys.runCycles(2 * System::CYCLES_PER_FRAME);
        check(cpu.isCartridgeInserted() && cpu.readByte(0x000000) != CPU::EMPTY_SLOT_EVEN && !cpu.isHalted(), "(cartridge present, CPU spinning in RAM)");
        const uint16_t pcBefore = cpu.getPC();
        const uint64_t instrBefore = cpu.getInstructionCount();
        sys.ejectCartridge();
        bool pattern = true;
        for (uint32_t a : {0x000000u, 0x000001u, 0x001234u, 0x00FFFEu, 0x05A5A6u, 0x7F0001u, 0xE20000u, 0xE20041u})
            pattern = pattern && cpu.readByte(a) == ((a & 1) ? CPU::EMPTY_SLOT_ODD : CPU::EMPTY_SLOT_EVEN);
        check(pattern, "banks $00-$7F and $E2 read CA FE CA FE...");
        check(cpu.readByte(0xC00000) == 0xFF && cpu.readByte(0x900000) == 0x00, "other unmapped banks are still $FF, RAM is untouched");
        check(CPU::EMPTY_SLOT_EVEN != 0xFF && CPU::EMPTY_SLOT_ODD != 0xFF && CPU::EMPTY_SLOT_EVEN != 0xDE && CPU::EMPTY_SLOT_ODD != 0xAF,
              "the pattern is neither HLT ($FF) nor the unverified-chunk poison");
        check(cpu.readByte(0xD80049) == 0 && cpu.readByte(0xD8004A) == 0 && cpu.readByte(0xD8004B) == 0, "CART_ENTRY reads 0");
        check(!cpu.isDataGatingActive() && !sys.hasCartridge(), "chunk gating is gone with the cartridge");
        check(cpu.getPC() == pcBefore && !cpu.isHalted() && cpu.getInstructionCount() == instrBefore, "the yank itself doesn't touch the CPU");
        // DMA out of the empty slot copies the pattern.
        cpu.writeByte(0xD80020, 0);
        const uint8_t cfg[9] = {0x00, 0x00, 0x00, 0x00, /*dst*/ 0x00, 0x10, 0x90, 0x01, 0x00};   // 256 bytes $00:0000 -> $90:1000
        for (uint8_t b : cfg) cpu.writeByte(0xD80021, b);
        sys.runCycles(20000);
        check(cpu.readByte(0x901000) == CPU::EMPTY_SLOT_EVEN && cpu.readByte(0x901001) == CPU::EMPTY_SLOT_ODD,
              "DMA from the empty slot reads the same pattern");
        const uint64_t i0 = cpu.getInstructionCount();
        sys.runCycles(2 * System::CYCLES_PER_FRAME);
        check(cpu.getInstructionCount() > i0 && !cpu.isHalted(), "the CPU keeps executing without a cartridge");
        sys.softReset();
        check(!cpu.isHalted() && cpu.getPB() == 0 && cpu.getPC() == 0, "softReset() on an empty slot lands at $00:0000, no crash");
        sys.runCycles(System::CYCLES_PER_FRAME);
        sys.hardReset();
        sys.runCycles(System::CYCLES_PER_FRAME);
        check(true, "hardReset() on an empty slot is fine");
    }

    std::printf("Stop 'n' Swop\n");
    {
        System sys;
        sys.setVerifySignatures(false);
        sys.insertCartridge(v1);
        sys.softReset();
        CPU& cpu = sys.getCPU();
        spinInRam(sys);
        sys.runCycles(3 * System::CYCLES_PER_FRAME);
        const uint8_t oldFirst = cpu.readByte(0x000000);
        const uint16_t pcBefore = cpu.getPC();
        const uint64_t cyclesBefore = sys.getMasterCycleCount();
        const uint8_t wramProbe = (uint8_t)(cpu.readByte(0x800000) + 1);
        cpu.writeByte(0x800000, wramProbe);
        check(sys.insertCartridge(v3), "inserting a second cartridge into a full slot works");
        check(cpu.isCartridgeInserted() && cpu.isDataGatingActive(), "the new cartridge is mapped and v3 gating is on");
        check(cpu.readByte(0x000000) == v3[64] && cpu.readByte(0x000001) == v3[65], "bank $00 now holds the new image");
        (void)oldFirst;
        check(cpu.getPC() == pcBefore && sys.getMasterCycleCount() == cyclesBefore && !cpu.isHalted(),
              "no reset, no boot, no verify: the CPU and the clock are exactly where they were");
        check(cpu.readByte(0x800000) == wramProbe, "work RAM untouched");
        const uint32_t entryV3 = v3[12] | (v3[13] << 8) | (v3[14] << 16);
        check((cpu.readByte(0xD80049) | (cpu.readByte(0xD8004A) << 8) | (cpu.readByte(0xD8004B) << 16)) == (int)entryV3,
              "CART_ENTRY follows the new cartridge");
        const uint32_t codeSize = v3[64 + (v3[8] | (v3[9] << 8) | (v3[10] << 16)) + 296] |
                                  (v3[64 + (v3[8] | (v3[9] << 8) | (v3[10] << 16)) + 297] << 8);
        check(cpu.readByte(codeSize) == 0xDE && cpu.readByte(codeSize + 1) == 0xAF, "its data chunks start unverified (poison)");
        // The same swap twice is a no-op on state except what was already running.
        System a, b;
        for (System* s : {&a, &b}) {
            s->setVerifySignatures(false);
            s->insertCartridge(v1);
            s->softReset();
            spinInRam(*s);
            s->runCycles(2 * System::CYCLES_PER_FRAME);
            s->insertCartridge(v3);
            s->runCycles(2 * System::CYCLES_PER_FRAME);
        }
        check(sameParts(a.stateHashParts(), b.stateHashParts()), "a swap replays identically on two Systems");
        check(!sys.insertCartridge(std::vector<uint8_t>(10, 0x5A)) && cpu.isCartridgeInserted() && cpu.readByte(0x000000) == v3[64],
              "a garbage image is refused and the slot is left alone");
    }

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
