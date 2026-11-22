// test_game_save: a real cartridge using its battery save RAM, end to end (docs/cart-save.md).
//
//   test_game_save <bootrom.bin|none> <game.zpb> <workdir> [--dev-unit] [--settle N]
//
// Runs ZPgameTemplate's game, which keeps the hero's position in 1 KB of save RAM (zp_save.h) and starts there, through the
// frontends' own .sav handling (SaveFile):
//   * run 1 has no game.sav: the hero starts at the default; the player holds RIGHT, the game writes the position to the SRAM,
//     and the file appears with a valid slot in it
//   * run 2 (a power cycle later: a new System, the same file) starts with the hero somewhere else: its first frames differ from
//     a run with no file, and the game does not rewrite a save that did not change
//   * run 3 with a save made by holding LEFT starts somewhere else again, and run 2 repeated is identical (deterministic)
//   * a wiped .sav (all $FF) starts like no file at all
// With a real Boot ROM give it --dev-unit (the template is dev-signed) and --settle ~700 (the splash takes a few seconds).
#include "save_file.h"
#include "system.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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

struct Setup {
    std::string boot, rom, savePath;
    bool devUnit = false;
    int settle = 200;
};

uint64_t frameHash(System& sys) {
    uint64_t h = 1469598103934665603ull;
    std::vector<Color32> row(FB_WIDTH);
    for (int y = 0; y < 256; y++) {
        if (!sys.getDisplay().getScanline(y, row.data())) std::fill(row.begin(), row.end(), 0u);
        for (Color32 px : row) h = (h ^ px) * 1099511628211ull;
    }
    return h;
}

// Power on a fresh System with the cartridge and whatever is in savePath, run `settle` frames, hold `dir` for `hold` frames, run
// `after` more frames with nothing held, flush the save. Returns the hash of the frame at the end (and the SRAM, if asked).
struct Result { uint64_t frame = 0; std::vector<uint8_t> sram; bool dirtyAfterBoot = false; bool ok = false; };

Result run(const Setup& s, uint8_t dir, int hold, int after) {
    Result r;
    System sys;
    sys.setVideoThread(false);
    sys.setIdlePollSkip(false);
    if (s.devUnit) sys.setDevUnit(true);
    else sys.setVerifySignatures(false);
    if (s.boot != "none" && !sys.loadBootROM(s.boot)) { std::fprintf(stderr, "cannot load the Boot ROM\n"); return r; }
    if (!sys.loadROM(s.rom)) { std::fprintf(stderr, "cannot load the cartridge\n"); return r; }
    SaveFile save;
    std::string note;
    save.attach(sys, s.savePath, note);
    if (!note.empty() || !save.active()) { std::fprintf(stderr, "save not attached: %s\n", note.c_str()); return r; }
    sys.powerOn();
    CPU& cpu = sys.getCPU();
    cpu.setPlayerInput(0, PlayerInput::CTRL_CONNECTION, 0);
    for (int f = 0; f < s.settle; f++) sys.stepFrame();
    r.dirtyAfterBoot = sys.saveDirty();
    if (hold) {
        cpu.setPlayerInput(dir, PlayerInput::CTRL_CONNECTION, 0);
        for (int f = 0; f < hold; f++) sys.stepFrame();
        cpu.setPlayerInput(0, PlayerInput::CTRL_CONNECTION, 0);
    }
    for (int f = 0; f < after; f++) sys.stepFrame();
    r.frame = frameHash(sys);
    r.sram = sys.saveData();
    save.flush(sys);
    r.ok = true;
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <bootrom.bin|none> <game.zpb> <workdir> [--dev-unit] [--settle N]\n", argv[0]);
        return 2;
    }
    Setup s;
    s.boot = argv[1];
    s.rom = argv[2];
    const std::string work = argv[3];
    for (int i = 4; i < argc; i++) {
        if (!std::strcmp(argv[i], "--dev-unit")) s.devUnit = true;
        else if (!std::strcmp(argv[i], "--settle") && i + 1 < argc) s.settle = std::atoi(argv[++i]);
    }
    namespace fs = std::filesystem;
    fs::create_directories(work);
    const std::string sav = work + "/game.sav", savLeft = work + "/left.sav", savWipe = work + "/wipe.sav", none = work + "/none.sav";
    for (const std::string& p : {sav, savLeft, savWipe, none}) fs::remove(p);

    std::printf("run 1: no game.sav, hold RIGHT, the game saves the hero's position\n");
    s.savePath = sav;
    const Result r1 = run(s, PlayerInput::DIR_RIGHT, 40, 150);
    check(r1.ok, "ran");
    check(fs::exists(sav) && slurp(sav).size() == 1024, "game.sav was written, 1024 bytes (the cartridge's SRAM)");
    const std::vector<uint8_t> file1 = slurp(sav);
    check(file1.size() == 1024 && (file1[0] == 0xA5 || file1[16] == 0xA5), "it holds a committed slot ($A5)");
    int slot = file1.size() == 1024 && file1[0] == 0xA5 ? 0 : 16;
    check(file1.size() == 1024 && file1[slot + 6] > 120 && file1[slot + 7] == 136, "the hero's x moved right of its start (120), y did not move");
    check(!r1.dirtyAfterBoot, "(a fresh cartridge: nothing written during boot before the player moved)");

    std::printf("run 2: a power cycle later, the same file\n");
    Setup s2 = s;
    const Result r2 = run(s2, 0, 0, 5);
    check(r2.ok && r2.sram == file1, "the game started with exactly the saved bytes in its SRAM");
    check(!r2.dirtyAfterBoot, "...and did not rewrite a save that did not change");
    s2.savePath = none;
    const Result rNone = run(s2, 0, 0, 5);
    check(rNone.ok && rNone.frame != r2.frame, "its first frames differ from a run with no file: the hero started where it was left");
    const Result r2b = run(s, 0, 0, 5);
    check(r2b.ok && r2b.frame == r2.frame, "repeating run 2 gives the identical frame");

    std::printf("run 3: a save made by holding LEFT\n");
    s2.savePath = savLeft;
    run(s2, PlayerInput::DIR_LEFT, 40, 150);
    const std::vector<uint8_t> fileL = slurp(savLeft);
    check(fileL.size() == 1024 && fileL != file1, "a different file");
    const Result r3 = run(s2, 0, 0, 5);
    check(r3.ok && r3.frame != r2.frame && r3.frame != rNone.frame, "it starts somewhere else again (not where RIGHT left it, not the default)");

    std::printf("a wiped save\n");
    {
        std::ofstream f(savWipe, std::ios::binary);
        const std::vector<uint8_t> ff(1024, 0xFF);
        f.write(reinterpret_cast<const char*>(ff.data()), 1024);
    }
    s2.savePath = savWipe;
    const Result rw = run(s2, 0, 0, 5);
    check(rw.ok && rw.frame == rNone.frame, "an erased ($FF) save starts exactly like no save at all");

    std::printf("%s (%d failure%s)\n", failures ? "FAIL" : "all ok", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
