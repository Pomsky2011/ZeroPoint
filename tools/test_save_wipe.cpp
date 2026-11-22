// test_save_wipe: the dev Boot ROM's save wipe (ZPbootROM/def88186/devwipe_dev.def, docs/cart-save.md).
//
//   test_save_wipe <bootrom_dev.bin> [<bootrom.bin>] [<workdir>]
//
// Holding all four shoulder inputs of player 1 (mask $78 of the control byte) while a DEV UNIT boots through the DEV Boot ROM erases
// the cartridge's save RAM to $FF before the cartridge starts. Three of the four, none, a unit that is not a dev unit, and the retail
// Boot ROM do not. The press is an ordinary controller input, so a movie of it replays the same wipe.
#include "movie.h"
#include "system.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace ZeroPoint;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

constexpr uint8_t ALL4 = PlayerInput::CTRL_BIGLEFT | PlayerInput::CTRL_BIGRIGHT | PlayerInput::CTRL_LITTLELEFT | PlayerInput::CTRL_LITTLERIGHT;

// An unsigned cartridge with 4 KiB of save RAM whose program is `jmp $00:0000` (it just spins). A dev unit's boot skips verification.
std::vector<uint8_t> spinRom(unsigned saveCode) {
    const uint8_t code[] = {0x10, 0x00, 0x00, 0x00};
    std::vector<uint8_t> img(64 + sizeof code, 0);
    std::memcpy(img.data(), "ZPB\0", 4);
    img[4] = 1;
    img[5] = uint8_t(saveCode << SAVE_CODE_SHIFT);
    img[6] = 64;
    img[8] = sizeof code;
    std::snprintf(reinterpret_cast<char*>(&img[0x10]), 32, "spin");
    std::memcpy(img.data() + 64, code, sizeof code);
    return img;
}

std::vector<uint8_t> pattern(size_t n) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; i++) v[i] = uint8_t(i * 7 + 3);
    return v;
}

struct Boot { std::vector<uint8_t> save; bool dirty = false; bool reachedCart = false; };

// Boot `rom` with `bootPath` (dev unit or not), the pad holding `ctrl`, a save of `pattern`; run 700 frames.
Boot boot(const std::string& bootPath, const std::vector<uint8_t>& rom, bool devUnit, uint8_t ctrl) {
    System sys;
    sys.setVideoThread(false);
    sys.setIdlePollSkip(false);
    sys.setDevUnit(devUnit);
    if (!sys.loadBootROM(bootPath)) { std::fprintf(stderr, "cannot load %s\n", bootPath.c_str()); return {}; }
    sys.loadROMImage(rom);
    sys.setSave(pattern(sys.saveSize()));
    sys.getCPU().setPlayerInput(0, PlayerInput::CTRL_CONNECTION | ctrl, 0);
    sys.powerOn();
    for (int f = 0; f < 700; f++) sys.stepFrame();
    Boot b;
    b.save = sys.saveData();
    b.dirty = sys.saveDirty();
    b.reachedCart = sys.getCPU().getPB() == 0x00 && sys.getCPU().getPC() < 0x10;
    return b;
}

bool allFF(const std::vector<uint8_t>& v) {
    for (uint8_t b : v) if (b != 0xFF) return false;
    return !v.empty();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <bootrom_dev.bin> [<bootrom.bin>] [<workdir>]\n", argv[0]); return 2; }
    const std::string devBoot = argv[1];
    const std::string retailBoot = argc > 2 ? argv[2] : "";
    const std::string work = argc > 3 ? argv[3] : ".";
    const std::vector<uint8_t> rom = spinRom(3);
    const std::vector<uint8_t> kept = pattern(4096);

    std::printf("dev Boot ROM on a dev unit\n");
    {
        const Boot none = boot(devBoot, rom, true, 0);
        check(none.reachedCart, "(the cartridge is reached: the boot completes)");
        check(none.save == kept && !none.dirty, "nothing held: the save is untouched");
        const Boot all = boot(devBoot, rom, true, ALL4);
        check(all.reachedCart, "(all four held: the boot still completes)");
        check(allFF(all.save), "all four held: the save is erased to $FF");
        check(all.dirty, "...and the host is told (the frontend writes the erased .sav)");
        const uint8_t bits[4] = {PlayerInput::CTRL_BIGLEFT, PlayerInput::CTRL_BIGRIGHT, PlayerInput::CTRL_LITTLELEFT, PlayerInput::CTRL_LITTLERIGHT};
        const char* names[4] = {"big left", "big right", "little left", "little right"};
        for (int skip = 0; skip < 4; skip++) {
            const Boot three = boot(devBoot, rom, true, uint8_t(ALL4 & ~bits[skip]));
            char what[96];
            std::snprintf(what, sizeof what, "all but %s: the save is untouched", names[skip]);
            check(three.reachedCart && three.save == kept, what);
        }
        const Boot others = boot(devBoot, rom, true, uint8_t(PlayerInput::CTRL_PAUSE | PlayerInput::CTRL_MENU));
        check(others.save == kept, "other control bits (menu, pause) do not count");
        const Boot big = boot(devBoot, spinRom(10), true, ALL4);
        check(big.reachedCart && allFF(big.save) && big.save.size() == 512 * 1024, "a 512 KiB save is erased whole");
        const Boot nosave = boot(devBoot, spinRom(0), true, ALL4);
        check(nosave.reachedCart && nosave.save.empty(), "a cartridge without save RAM boots as usual (nothing there to erase)");
    }

    std::printf("not a dev unit\n");
    {
        const Boot retailUnit = boot(devBoot, rom, false, ALL4);
        check(retailUnit.save.size() == 4096 && retailUnit.save == kept, "the dev Boot ROM on a retail unit does nothing (it also verifies: the cartridge never starts)");
    }
    if (!retailBoot.empty()) {
        std::printf("retail Boot ROM\n");
        const Boot r = boot(retailBoot, rom, true, ALL4);
        check(r.save == kept, "the retail Boot ROM never erases it, whatever is held and whatever the latch says");
    }

    std::printf("movies\n");
    {
        const std::string cart = work + "/spin_wipe.zpb";
        {
            std::ofstream f(cart, std::ios::binary);
            f.write(reinterpret_cast<const char*>(rom.data()), (std::streamsize)rom.size());
        }
        System sys;
        MovieSession rec;
        MovieSession::RecordSetup setup;
        setup.bootRomPath = devBoot;
        setup.cartridgePath = cart;
        setup.devUnit = true;
        setup.save = kept;
        setup.checkpointInterval = System::CYCLES_PER_FRAME * 60;
        std::string err;
        check(rec.beginRecording(sys, setup, err), err.empty() ? "recording starts on a dev unit with the dev Boot ROM" : err.c_str());
        rec.input(0, 0, PlayerInput::CTRL_CONNECTION | ALL4, 0);
        for (int f = 0; f < 700; f++) rec.run(System::CYCLES_PER_FRAME);
        const std::vector<uint8_t> afterRec = sys.saveData();
        const uint64_t recHash = sys.machineHash();
        const Movie movie = rec.finishRecording();
        check(allFF(afterRec), "the recorded session erased the save");
        check(movie.roms.size() == 1 && movie.roms[0].save == kept, "the movie carries the save it started with");
        RomLibrary lib;
        lib.add(work);
        lib.add(devBoot);
        System sys2;
        MovieSession play;
        check(play.beginPlayback(sys2, movie, lib, err), err.empty() ? "playback starts" : err.c_str());
        while (!play.finished() && !play.desynced() && play.failure().empty()) play.run(System::CYCLES_PER_FRAME);
        check(!play.desynced() && play.failure().empty() && play.checkpointsVerified() > 0, "every checkpoint verified");
        check(allFF(sys2.saveData()) && sys2.machineHash() == recHash, "the replay erases the save the same way: same final machine hash");
    }

    std::printf("%s (%d failure%s)\n", failures ? "FAIL" : "all ok", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
