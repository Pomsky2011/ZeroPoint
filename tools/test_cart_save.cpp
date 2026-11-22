// test_cart_save: the cartridge's battery SRAM (docs/cart-save.md).
//
//   test_cart_save [workdir]       (the .sav tests write there; default: the current directory)
//
//   * the header's save-size code (flags bits 4-7) picks the size; a code no cartridge can use is refused
//   * banks $C0-$C7 mirror the SRAM across the 512 KiB window; a cartridge without SRAM leaves them unmapped (open bus, as
//     on revision 13) and has no "save" hash part
//   * a new cartridge's SRAM is blank ($FF); setSave() fills it, refuses a wrong size; a changed byte raises the dirty flag
//   * softReset() and hardReset() keep it (a hardReset machine equals a fresh one holding the same save)
//   * a yank takes it away and the window reads CA FE; Stop 'n' Swop gives the new cartridge its own (blank) SRAM
//   * SaveFile, the host's .sav: picks the file up, writes only changes, atomically, never touches a file of the wrong size
#include "save_file.h"
#include "system.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace ZeroPoint;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

// An unsigned .zpb image (never booted: these tests drive the bus) whose header asks for 512 << saveCode bytes of SRAM.
std::vector<uint8_t> makeRom(unsigned saveCode, const char* title = "save test") {
    std::vector<uint8_t> img(64 + 1024, 0xEA);
    std::memset(img.data(), 0, 64);
    std::memcpy(img.data(), "ZPB\0", 4);
    img[4] = 1;                                    // version: unsigned
    img[5] = uint8_t(saveCode << SAVE_CODE_SHIFT);
    img[6] = 64;                                   // header size
    img[8] = 0x00; img[9] = 0x04;                  // ROM size 1024
    std::strncpy(reinterpret_cast<char*>(&img[0x10]), title, 31);
    return img;
}

bool hasPart(System& sys, const char* name) {
    for (const auto& p : sys.stateHashParts()) if (std::strcmp(p.first, name) == 0) return true;
    return false;
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

constexpr uint32_t SAVE = 0xC00000;
std::string work = ".";

}  // namespace

namespace {

// header
void section0() {
    std::printf("header\n");
        check(saveSizeForCode(0) == 0 && saveSizeForCode(1) == 1024 && saveSizeForCode(4) == 8192 && saveSizeForCode(10) == 512 * 1024,
              "code n means 512 << n bytes (0 = none, 1 = 1 KiB, 10 = 512 KiB)");
        ROM rom;
        check(rom.loadMemory(makeRom(0).data(), makeRom(0).size()) && rom.getSaveSize() == 0, "a ROM that predates save RAM (code 0) has none");
        const auto r4 = makeRom(4);
        check(rom.loadMemory(r4.data(), r4.size()) && rom.getSaveSize() == 8192, "code 4 = 8 KiB");
        const auto bad = makeRom(11);
        check(!rom.loadMemory(bad.data(), bad.size()), "code 11 is refused");
        auto signedFlag = makeRom(2);
        signedFlag[5] |= 0x01;   // bit 0 is zpbuild's "signed" flag and must not be mistaken for a size
        check(rom.loadMemory(signedFlag.data(), signedFlag.size()) && rom.getSaveSize() == 2048, "flags bit 0 (signed) is not part of the size");
}

}  // namespace

namespace {

// no save RAM: the window stays unmapped
void section1() {
    std::printf("no save RAM: the window stays unmapped\n");
        System sys;
        CPU& cpu = sys.getCPU();
        check(sys.insertCartridge(makeRom(0)), "insert a cartridge without SRAM");
        check(sys.saveSize() == 0, "saveSize() is 0");
        check(cpu.readByte(SAVE) == 0xFF && cpu.readByte(SAVE + 0x7FFFF) == 0xFF, "$C0-$C7 read open bus ($FF)");
        cpu.writeByte(SAVE + 5, 0x12);
        check(cpu.readByte(SAVE + 5) == 0xFF && !sys.saveDirty(), "a write goes nowhere (and is not 'dirty')");
        check(!hasPart(sys, "save"), "no 'save' hash part (every hash is what it was on revision 13)");
        check(!sys.setSave(std::vector<uint8_t>(1024, 0)), "setSave() is refused");
}

}  // namespace

namespace {

// blank, mirroring, dirty
void section2() {
    std::printf("blank, mirroring, dirty\n");
        System sys;
        CPU& cpu = sys.getCPU();
        check(sys.insertCartridge(makeRom(4)), "insert a cartridge with 8 KiB of SRAM");
        check(sys.saveSize() == 8192 && hasPart(sys, "save"), "saveSize() = 8192, and the machine hash covers it");
        bool blank = true;
        for (uint32_t i = 0; i < 0x80000; i += 0x101) blank &= cpu.readByte(SAVE + i) == 0xFF;
        check(blank, "a new cartridge's SRAM reads $FF across the whole window");
        check(cpu.readByte(0xC80000) == 0xFF && cpu.readByte(0xBDFFFF) == 0xFF, "(banks $BD and $C8 are not the save: open bus)");
        cpu.writeByte(SAVE + 0x0010, 0x5A);
        check(sys.saveDirty(), "a changed byte raises the dirty flag");
        check(sys.takeSaveDirty() && !sys.saveDirty(), "takeSaveDirty() clears it");
        cpu.writeByte(SAVE + 0x0010, 0x5A);
        check(!sys.saveDirty(), "writing the same value again is not a change");
        check(cpu.readByte(SAVE + 0x2010) == 0x5A && cpu.readByte(0xC10010) == 0x5A && cpu.readByte(0xC7E010) == 0x5A,
              "8 KiB mirrored every 8 KiB across all eight banks");
        check(sys.saveData()[0x10] == 0x5A, "saveData() sees the guest's write");
        cpu.writeByte(0xC7FFFF, 0x33);
        check(sys.saveData()[0x1FFF] == 0x33 && cpu.readByte(SAVE + 0x1FFF) == 0x33, "the last byte of the window aliases the last SRAM byte");

        const uint64_t h0 = sys.machineHash();
        cpu.writeByte(SAVE + 0x0011, 0x77);
        check(sys.machineHash() != h0, "the save is part of machineHash()");

        std::vector<uint8_t> mine(8192);
        for (size_t i = 0; i < mine.size(); i++) mine[i] = uint8_t(i * 5 + 1);
        check(!sys.setSave(std::vector<uint8_t>(4096, 0)) && !sys.setSave(std::vector<uint8_t>(16384, 0)), "setSave() with the wrong size is refused");
        check(sys.saveData()[0x11] == 0x77, "...and changes nothing");
        check(sys.setSave(mine) && cpu.readByte(SAVE + 0x0003) == mine[3] && cpu.readByte(0xC30003) == mine[3], "setSave() with the right size loads it onto the bus");
}

}  // namespace

namespace {

// sizes
void section3() {
    std::printf("sizes\n");
        for (unsigned code : {1u, 7u, 10u}) {
            System sys;
            CPU& cpu = sys.getCPU();
            const size_t size = saveSizeForCode(code);
            sys.insertCartridge(makeRom(code));
            for (uint32_t bank = 0; bank < 8; bank++) cpu.writeByte(SAVE + bank * 0x10000 + 0x0100, uint8_t(0x10 + bank));
            bool ok = true;
            // the byte at window offset w lives at saveData()[w & (size - 1)]
            for (uint32_t w = 0; w < 0x80000 && ok; w += 0x1111) ok &= cpu.readByte(SAVE + w) == sys.saveData()[w & (size - 1)];
            char name[96];
            std::snprintf(name, sizeof name, "%zu bytes: window offset w reads saveData()[w & (size-1)]", size);
            check(ok, name);
            check(sys.saveSize() == size, "saveSize() matches the code");
        }
}

}  // namespace

namespace {

// DMA and code in the save window
void sectionDmaAndCode() {
    std::printf("DMA into it, and code running from it\n");
    auto sysP = std::make_unique<System>();
    System& sys = *sysP;
    CPU& cpu = sys.getCPU();
    sys.insertCartridge(makeRom(4));
    for (int i = 0; i < 256; i++) cpu.writeByte(0x800400 + i, uint8_t(i ^ 0x3C));
    cpu.writeByte(0xD80020, 0);   // reset the config write index, then 9 bytes: mode, src (3), dst (3), size multiplier, trigger
    const uint8_t cfg[9] = {0x00, 0x00, 0x04, 0x80, 0x00, 0x01, 0xC0, 0x01, 0x00};   // DataCopy $80:0400 -> $C0:0100, 256 bytes
    for (uint8_t b : cfg) cpu.writeByte(0xD80021, b);
    sys.runCycles(20000);
    bool same = true;
    for (int i = 0; i < 256; i++) same &= cpu.readByte(SAVE + 0x100 + i) == uint8_t(i ^ 0x3C);
    check(same, "a DMA transfer writes the save RAM");
    check(sys.saveData()[0x100] == 0x3C && sys.saveDirty(), "...it reaches saveData() and raises the dirty flag");

    // lda $D80032 ; sta $80:0300 ; jmp $C0:0000 - stored in the SRAM, then run from it
    const uint8_t code[] = {0x3D, 0x32, 0x00, 0xD8, 0x55, 0x00, 0x03, 0x80, 0x10, 0x00, 0x00, 0xC0};
    for (size_t i = 0; i < sizeof code; i++) cpu.writeByte(SAVE + i, code[i]);
    cpu.setPlayerInput(0, PlayerInput::CTRL_CONNECTION, PlayerInput::BTN_3);
    CPUFlags f = cpu.getFlags();
    f.I = true;
    cpu.setFlags(f);
    cpu.setPB(0xC0);
    cpu.setPC(0x0000);
    if (cpu.isHalted()) cpu.reset(), cpu.setFlags(f), cpu.setPB(0xC0), cpu.setPC(0x0000);   // (as test_cart_slot's spinInRam)
    cpu.writeByte(0x800300, 0);
    sys.runCycles(2000);
    check(cpu.readByte(0x800300) == PlayerInput::BTN_3 && cpu.getPB() == 0xC0, "the CPU runs a program stored in the save RAM");
}

}  // namespace

namespace {

// resets keep it
void section4() {
    std::printf("resets keep it\n");
        System sys;
        sys.setVerifySignatures(false);
        CPU& cpu = sys.getCPU();
        sys.insertCartridge(makeRom(3));
        std::vector<uint8_t> mine(4096);
        for (size_t i = 0; i < mine.size(); i++) mine[i] = uint8_t(i ^ 0xA5);
        sys.setSave(mine);
        sys.softReset();
        check(cpu.readByte(SAVE + 0x20) == mine[0x20] && sys.saveData() == mine, "softReset() keeps it");
        sys.hardReset();
        check(cpu.readByte(SAVE + 0x20) == mine[0x20] && cpu.readByte(0xC40020) == mine[0x20] && sys.saveData() == mine, "hardReset() (a power cycle) keeps it, mapped");

        System fresh;
        fresh.setVerifySignatures(false);
        fresh.insertCartridge(makeRom(3));
        fresh.setSave(mine);
        fresh.softReset();
        std::string diff;
        const bool same = sameParts(fresh.stateHashParts(), sys.stateHashParts(), &diff);
        check(same, same ? "a hardReset() machine equals a fresh one holding the same save" : ("hardReset() differs from fresh in: " + diff).c_str());
}

}  // namespace

namespace {

// yank and Stop 'n' Swop
void section5() {
    std::printf("yank and Stop 'n' Swop\n");
        System sys;
        CPU& cpu = sys.getCPU();
        sys.insertCartridge(makeRom(2, "first"));
        cpu.writeByte(SAVE + 1, 0x99);
        const std::vector<uint8_t> kept = sys.saveData();
        sys.ejectCartridge();
        check(sys.saveSize() == 0 && !sys.hasCartridge(), "yanking the cartridge takes its SRAM with it");
        check(cpu.readByte(SAVE) == 0xCA && cpu.readByte(SAVE + 1) == 0xFE && cpu.readByte(0xC7FFFE) == 0xCA, "the window now reads CA FE like the ROM banks");
        check(!hasPart(sys, "save"), "no 'save' hash part without a cartridge");
        cpu.writeByte(SAVE, 0x00);
        check(cpu.readByte(SAVE) == 0xCA, "writes go nowhere");

        sys.insertCartridge(makeRom(2, "first"));
        check(cpu.readByte(SAVE + 1) == 0xFF && sys.saveData() != kept, "a cartridge put back starts blank: the host restores its .sav");
        check(sys.setSave(kept) && cpu.readByte(SAVE + 1) == 0x99, "setSave() puts the old bytes back");

        sys.insertCartridge(makeRom(0, "second"));   // Stop 'n' Swop to a cartridge without SRAM
        check(sys.saveSize() == 0 && cpu.readByte(SAVE + 1) == 0xFF, "swapped to a cartridge without SRAM: the window is open bus again");
        sys.insertCartridge(makeRom(5, "third"));
        check(sys.saveSize() == 16384 && cpu.readByte(SAVE + 1) == 0xFF, "swapped to one with 16 KiB: its own blank SRAM");
        sys.insertCartridge(makeRom(0, "no save"));
        sys.ejectCartridge();
        check(cpu.readByte(SAVE) == 0xFF, "yanking a cartridge that had no SRAM leaves the window at open bus, as on revision 13");
        sys.hardReset();
        System fresh;
        std::string diff;
        const bool same = sameParts(fresh.stateHashParts(), sys.stateHashParts(), &diff);
        check(same, same ? "a power cycle after a yank equals a fresh System" : ("differs from fresh in: " + diff).c_str());
}

}  // namespace

namespace {

// .sav files (SaveFile). (The Systems are on the heap: each is a megabyte or more, and four on the stack overflow it under a sanitizer.)
void section6() {
    std::printf(".sav files (SaveFile)\n");
        namespace fs = std::filesystem;
        auto slurp = [](const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        };
        auto put = [](const std::string& path, const std::vector<uint8_t>& d) {
            std::ofstream f(path, std::ios::binary);
            f.write(reinterpret_cast<const char*>(d.data()), (std::streamsize)d.size());
        };
        check(SaveFile::pathFor("dir/game.zpb") == "dir/game.sav" && SaveFile::pathFor("game") == "game.sav", "pathFor() swaps the extension for .sav");
        check(SaveFile::sizeFor(makeRom(3)) == 4096 && SaveFile::sizeFor(makeRom(0)) == 0, "sizeFor() reads the header");

        const std::string path = work + "/cart_save_test.sav";
        fs::remove(path);
        auto sys_p = std::make_unique<System>();
        System& sys = *sys_p;
        sys.insertCartridge(makeRom(3));   // 4 KiB
        CPU& cpu = sys.getCPU();
        SaveFile sf;
        std::string note;
        sf.attach(sys, path, note);
        check(sf.active() && note.empty() && sys.saveData() == std::vector<uint8_t>(4096, 0xFF), "no file yet: attached, SRAM blank");
        check(sf.flush(sys) && !fs::exists(path), "nothing changed: no file is created");
        cpu.writeByte(SAVE + 7, 0x42);
        sf.poll(sys, 100.0);
        check(fs::exists(path) && slurp(path) == sys.saveData(), "a change is written (poll, first time)");
        cpu.writeByte(SAVE + 8, 0x43);
        sf.poll(sys, 100.5);
        check(slurp(path)[8] == 0xFF, "...but not again within the interval");
        sf.poll(sys, 100.5 + SaveFile::FLUSH_INTERVAL_SECONDS);
        check(slurp(path)[8] == 0x43, "...once the interval has passed it is");
        check(!fs::exists(path + ".tmp"), "no temporary file left behind");

        // a second machine picks it up
        auto sys2_p = std::make_unique<System>();
        System& sys2 = *sys2_p;
        sys2.insertCartridge(makeRom(3));
        SaveFile sf2;
        sf2.attach(sys2, path, note);
        check(sf2.active() && sys2.saveData() == sys.saveData() && sys2.getCPU().readByte(SAVE + 7) == 0x42, "attach() loads the file into the cartridge");
        check(!sys2.saveDirty(), "what was just loaded is not a change");
        sys2.getCPU().writeByte(SAVE + 9, 0x44);
        check(sf2.flush(sys2) && slurp(path)[9] == 0x44, "flush() writes a change at once");
        const auto before = slurp(path);
        check(sf2.flush(sys2) && slurp(path) == before, "(and a flush with nothing new changes nothing)");

        // a yank: flush before it, and the empty slot has nothing to write
        sys2.ejectCartridge();
        check(sf2.flush(sys2, true) && slurp(path) == before, "after a yank there is nothing of the cartridge to write: the file stays");

        // a file of the wrong size is never overwritten
        put(path, std::vector<uint8_t>(100, 0x77));
        auto sys3_p = std::make_unique<System>();
        System& sys3 = *sys3_p;
        sys3.insertCartridge(makeRom(3));
        SaveFile sf3;
        note.clear();
        sf3.attach(sys3, path, note);
        sys3.getCPU().writeByte(SAVE, 0x12);
        sf3.flush(sys3, true);
        check(!sf3.active() && !note.empty() && slurp(path) == std::vector<uint8_t>(100, 0x77), "a file of the wrong size is reported and left alone");

        // no SRAM, no path, detach
        auto sys4_p = std::make_unique<System>();
        System& sys4 = *sys4_p;
        sys4.insertCartridge(makeRom(0));
        SaveFile sf4;
        sf4.attach(sys4, path, note);
        check(!sf4.active(), "a cartridge without SRAM never binds a file");
        sf3.attach(sys3, "", note);
        check(!sf3.active(), "an empty path (saves off) stays inactive");
        sf2.detach();
        check(!sf2.active() && sf2.flush(sys2, true), "detached: flush is a no-op");
        fs::remove(path);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) work = argv[1];
    section0();
    section1();
    section2();
    section3();
    section4();
    sectionDmaAndCode();
    section5();
    section6();

    std::printf("%s (%d failure%s)\n", failures ? "FAIL" : "all ok", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
