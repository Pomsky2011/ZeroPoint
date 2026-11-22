#pragma once
// The host end of a cartridge's battery SRAM (docs/cart-save.md): the .sav file next to the ROM. Kept out of System on
// purpose (files are the host's business; the machine only knows its bytes and a dirty flag). A frontend does:
//
//     SaveFile save;
//     system.loadROM(path);  save.attach(system, SaveFile::pathFor(path), note);   // before powerOn(): picks up the file
//     ... every tick:        save.poll(system, wallSeconds);                         // flushes a changed save every few seconds
//     ... before a yank / swap / exit:  save.flush(system, true);
//
// Movies never go through this: a recording takes its starting save with SaveFile::load() and a replay carries its own.
#include "system.h"
#include <string>
#include <vector>

namespace ZeroPoint {

class SaveFile {
public:
    static constexpr double FLUSH_INTERVAL_SECONDS = 2.0;   // a changed save reaches the disk at most this late

    // "dir/game.zpb" -> "dir/game.sav" (a path with no extension just gets ".sav").
    static std::string pathFor(const std::string& romPath);
    // The SRAM size a .zpb image asks for (0: none, or not a valid image).
    static size_t sizeFor(const std::vector<uint8_t>& romImage);
    // Reads `path` into `out`. True only if the file exists and is exactly `expectedSize` bytes; if it exists with another
    // size `note` says so (an absent file is not worth a note).
    static bool load(const std::string& path, size_t expectedSize, std::vector<uint8_t>& out, std::string& note);

    // Binds to the cartridge in `sys`'s slot (call after loadROM()/insertCartridge()): loads `path` into it if the file
    // is there and fits. An empty path, or a cartridge without SRAM, leaves this inactive. A file of the wrong size is
    // never overwritten: it is reported in `note` and this stays inactive.
    void attach(System& sys, const std::string& path, std::string& note);
    // Unbinds without writing (a movie has taken over, or the cartridge is gone).
    void detach();
    bool active() const { return bound; }
    const std::string& path() const { return filePath; }
    // The last write error, "" if the last write worked.
    const std::string& error() const { return lastError; }

    // Writes the file if the save changed since the last write (or `force`): to a temporary beside it, then renamed over
    // it, so a crash never leaves half a save. False if the write failed (it is retried at the next flush/poll).
    bool flush(System& sys, bool force = false);
    // Once per tick with a wall clock in seconds: flushes a changed save once FLUSH_INTERVAL_SECONDS have passed.
    void poll(System& sys, double nowSeconds);

private:
    bool bound = false;
    bool pending = false;       // the machine's save differs from the file
    double lastWrite = -1e9;
    std::string filePath;
    std::string lastError;
};

}  // namespace ZeroPoint
