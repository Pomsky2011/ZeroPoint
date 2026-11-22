#pragma once
// Input movies (.zpm): a recording of everything a player can do to the console, replayable bit-for-bit.
// docs/movies.md has the format and the guarantees; docs/determinism-proof.md has why replay can't desync.
//
// A movie is a power-on setup (which boot ROM / APU BIOS / cartridge, pinned by SHA-256), then events at exact master
// cycles of *movie time* (cycles since power-on; it keeps counting while a halted CPU freezes the machine, and across
// resets): controller changes for both ports, bytes typed into the debug serial port (dev unit only), soft/hard reset,
// cartridge yank, Stop 'n' Swop. Checkpoints (hashes of the
// machine state) let a replay name the cycle at which it first goes wrong.
#include "system.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ZeroPoint {

using Sha256 = std::array<uint8_t, 32>;
Sha256 sha256Of(const uint8_t* data, size_t size);
std::string toHex(const Sha256& h);
bool fromHex(const std::string& s, Sha256& out);

struct MovieEvent {
    enum Type : uint8_t { Input = 1, SoftReset = 2, HardReset = 3, Yank = 4, Insert = 5, Checkpoint = 6, SerialIn = 7 };
    uint64_t cycle = 0;                 // movie time, master cycles since power-on
    Type type = Input;
    uint8_t port = 0;                   // Input: 0 = player 1, 1 = player 2
    uint8_t dir = 0, ctrl = 0, btn = 0; // Input: the three controller bytes (PlayerInput::)
    uint8_t data = 0;                   // SerialIn: the byte the host sends to the debug serial port
    uint32_t rom = 0;                   // Insert: index into Movie::roms
    std::vector<uint64_t> parts;        // Checkpoint: System::machineHashParts() values, in order
};

struct MovieRom {
    Sha256 sha{};                       // of the whole .zpb file
    std::string title;                  // informational
    // The cartridge's battery SRAM as it was when this ROM went into the slot (docs/cart-save.md): the bytes themselves, so a
    // replay starts from exactly the save the recording did. Empty = blank ($FF), which is also what every ROM without SRAM has.
    std::vector<uint8_t> save;
};

struct Movie {
    uint32_t coreRevision = System::CORE_REVISION;
    uint64_t lengthCycles = 0;          // the movie ends here
    bool verifySignatures = true;       // System::setVerifySignatures for the replay
    bool hleBoot = true;                // true: native boot ROM; false: bootRom below
    bool hleApuBios = true;             // true: native APU BIOS; false: apuBios below
    bool devUnit = false;               // System::setDevUnit for the replay (debug UART, signature checks always pass)
    Sha256 bootRom{}, apuBios{};        // pinned images when not HLE
    int32_t initialRom = -1;            // index into roms, -1 = empty slot at power-on
    std::vector<MovieRom> roms;
    std::string author, description;
    uint64_t checkpointInterval = 0;    // cycles between checkpoints when recorded (0 = none); informational
    std::vector<MovieEvent> events;     // by cycle; same-cycle events apply in this order

    std::vector<uint8_t> serialize() const;
    static bool parse(const uint8_t* data, size_t size, Movie& out, std::string& error);
    bool save(const std::string& path, std::string& error) const;
    static bool load(const std::string& path, Movie& out, std::string& error);

    // Text form (for authoring and diffing; see docs/movies.md). fromText hashes the files it names.
    std::string toText() const;
    static bool fromText(const std::string& text, Movie& out, std::string& error);
};

// Finds the files a movie pins by hash: the ROM images, boot ROM and APU BIOS. Add files or directories (scanned
// recursively); lookups are by content, so file names don't matter.
class RomLibrary {
public:
    void add(const std::string& fileOrDirectory);
    bool read(const Sha256& sha, std::vector<uint8_t>& out) const;     // false if not found
    std::string pathOf(const Sha256& sha) const;                        // "" if not found
    size_t size() const { return files.size(); }
private:
    struct Entry { Sha256 sha; std::string path; };
    std::vector<Entry> files;
};

// Plays or records a movie on a System. The frontend runs the machine *only* through run() (so movie time and
// machine time can't disagree) and sends host actions through input()/softReset()/hardReset()/yank()/insert(); in
// playback those are ignored (the movie is in control), in recording they are applied and logged at the current cycle.
class MovieSession {
public:
    enum class Mode { Idle, Recording, Playing };

    struct RecordSetup {
        std::string bootRomPath;        // "" = native boot ROM
        std::string apuBiosPath;        // "" = native APU BIOS
        std::string cartridgePath;      // "" = empty slot at power-on
        std::vector<uint8_t> save;      // the cartridge's SRAM contents at power-on ({} = blank); must match its size
        bool verifySignatures = true;
        bool devUnit = false;           // record on a dev unit (needed for the debug serial port)
        uint64_t checkpointInterval = System::CYCLES_PER_FRAME * 64;
        std::string author, description;
    };

    // `sys` must be freshly constructed. Both set the machine up the same way (boot ROM, BIOS, verify flag, cartridge,
    // powerOn(), controllers idle) so a recording replays from exactly its own starting state.
    bool beginRecording(System& sys, const RecordSetup& setup, std::string& error);
    bool beginPlayback(System& sys, const Movie& movie, const RomLibrary& library, std::string& error,
                       bool ignoreCoreRevision = false);

    Mode mode() const { return currentMode; }
    uint64_t cycle() const { return movieCycle; }

    // Advance movie time by up to `cycles` master cycles (less if playback ends first), applying/recording events at
    // their exact cycles. Returns the cycles actually run.
    uint64_t run(uint64_t cycles);

    // Host actions. Recording: apply + log at cycle(). Playing: ignored.
    void input(int port, uint8_t dir, uint8_t ctrl, uint8_t btn);
    void softReset();
    void hardReset();
    void yank();
    // Host -> guest byte on the debug serial port. Recording: apply + log at cycle(). Playing: ignored. (Without a movie
    // a frontend calls System::serialReceive() itself.) Only does anything on a dev unit.
    void serialIn(uint8_t byte);
    // `save` is the new cartridge's SRAM contents ({} = blank); it goes into the movie so the replay swaps in the same bytes.
    bool insert(const std::vector<uint8_t>& image, const std::string& title, const std::vector<uint8_t>& save = {});

    // Playback state.
    bool finished() const { return currentMode == Mode::Playing && movieCycle >= movie.lengthCycles; }
    bool desynced() const { return desync; }
    const std::string& desyncReport() const { return desyncText; }   // which checkpoint, when, which chip
    size_t checkpointsVerified() const { return verified; }
    const std::string& failure() const { return failureText; }        // a fatal playback error (missing ROM, ...)

    // Recording: the movie so far; `lengthCycles` is set to the current cycle.
    Movie finishRecording();
    const Movie& current() const { return movie; }

private:
    System* sys = nullptr;
    Mode currentMode = Mode::Idle;
    Movie movie;
    const RomLibrary* library = nullptr;
    uint64_t movieCycle = 0;
    size_t nextEvent = 0;               // playback: next unapplied event
    uint64_t nextCheckpoint = 0;        // recording: movie cycle of the next checkpoint (0 = off)
    uint8_t lastInput[2][3] = {{0, PlayerInput::CTRL_CONNECTION, 0}, {0, 0, 0}};
    std::vector<std::vector<uint8_t>> romImages;   // by Movie::roms index (recording keeps what it inserted)
    bool desync = false;
    std::string desyncText, failureText;
    size_t verified = 0;

    bool setupMachine(const RecordSetup& s, const std::vector<uint8_t>* bootImage, const std::vector<uint8_t>* apuImage,
                      const std::vector<uint8_t>* cartImage, std::string& error);
    bool applyEvent(const MovieEvent& e);
    void log(MovieEvent e);
    int addRom(const std::vector<uint8_t>& image, const std::string& title, const std::vector<uint8_t>& save);
    bool applySave(const std::vector<uint8_t>& save, std::string& error);
    uint64_t runMachine(uint64_t cycles);
};

}  // namespace ZeroPoint
