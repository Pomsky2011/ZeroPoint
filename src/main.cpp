#define SDL_MAIN_HANDLED
#include "system.h"
#include "display.h"
#include "window.h"
#include "movie.h"
#include "serial_host.h"
#include <chrono>
#include "frame_pacer.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace ZeroPoint;

// Draw a static gradient so the windowed app is still useful without a ROM.
static void fillTestPattern(Display& display) {
    for (int y = 0; y < FULL_HEIGHT; y++) {
        for (int x = 0; x < FB_WIDTH; x++) {
            uint16_t red = (x * 31 / 255) << 1;
            uint16_t blue = (y * 31 / 255) << 11;
            display.setPixel(x, y, red | blue);
        }
    }
}

// Fallback path (no ROM): present the test pattern, no CPU/PPU/APU execution.
// There's no System/APU here to generate real audio, so play a quiet 440 Hz
// tone instead - just enough to confirm the audio device itself works.
static int runTestPattern(Window& window) {
    Display display;
    fillTestPattern(display);
    std::cout << "No ROM provided - showing test pattern. "
                 "Pass a .rom file to run the emulator.\n";

    static constexpr int16_t TONE_AMPLITUDE = 300;  // ~1% of full scale
    static constexpr double TONE_FREQ_HZ = 440.0;
    const int framesPerTick = System::AUDIO_SAMPLE_RATE * 16 / 1000;
    const double phaseStep = 2.0 * M_PI * TONE_FREQ_HZ / System::AUDIO_SAMPLE_RATE;
    double phase = 0.0;
    std::vector<int16_t> tone(framesPerTick * 2);

    while (!window.shouldClose()) {
        window.pollEvents();
        window.render(display);

        for (int i = 0; i < framesPerTick; i++) {
            int16_t sample = static_cast<int16_t>(TONE_AMPLITUDE * std::sin(phase));
            tone[i * 2] = sample;
            tone[i * 2 + 1] = sample;
            phase += phaseStep;
            if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
        }
        window.queueAudio(tone.data(), framesPerTick);

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    return 0;
}

int main(int argc, char** argv) {
    std::cout << "ZeroPoint Emulator\n";
    std::cout << "==================\n";
    std::cout << "Usage: zeropoint_sdl [rom.rom] [--dev] [--scale N] [--boot boot.bin] [--apu-bios bios.bin] [--no-verify] [--no-idle-skip]\n";
    std::cout << "       [--dev-unit [--serial-out FILE|-] [--serial-in FILE|-] [--serial-tcp PORT]]\n";
    std::cout << "       [--two-players] [--speed] [--swap-rom rom.zpb]... [--record out.zpm [--author NAME]] [--play in.zpm [--roms PATH]...] [--frames N]\n";
    std::cout << "Keys: arrows/z x c v/a s d f/shift/enter = player 1 (i j k l/7 8 9 0/u o n m = player 2 with --two-players)\n";
    std::cout << "      F1 soft reset, F2 hard reset (power cycle), F3 yank the cartridge, F4 Stop 'n' Swop (next --swap-rom)\n";
    std::cout << "Press ESC to exit\n\n";

    std::string romPath;
    std::string bootPath;
    std::string apuBiosPath;
    bool devMode = false;
    bool noVerify = false;
    bool devUnit = false;                            // --dev-unit: debug serial UART, signature checks always pass (docs/debug-protocol.md)
    std::vector<std::string> serialOuts, serialIns;
    long serialTcpPort = -1;
    bool idleSkip = System::idlePollSkipDefault();   // --no-idle-skip turns the poll fast-forward off (docs/idle-poll-skip.md)
    bool twoPlayers = false;
    bool showSpeed = false;
    std::string recordPath, playPath, author;
    long maxTicks = -1;   // --frames N: stop by itself after N 60 Hz ticks (headless runs, CI)
    std::vector<std::string> romDirs, swapRoms;
    int scale = 2;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--dev") == 0) {
            devMode = true;
        } else if (std::strcmp(argv[i], "--dev-unit") == 0) {
            devUnit = true;
        } else if (std::strcmp(argv[i], "--serial-out") == 0 && i + 1 < argc) {
            serialOuts.push_back(argv[++i]);
        } else if (std::strcmp(argv[i], "--serial-in") == 0 && i + 1 < argc) {
            serialIns.push_back(argv[++i]);
        } else if (std::strcmp(argv[i], "--serial-tcp") == 0 && i + 1 < argc) {
            serialTcpPort = std::atol(argv[++i]);
        } else if (std::strcmp(argv[i], "--no-verify") == 0) {
            noVerify = true;   // run unsigned/badly signed ROMs (HLE boot only; a real --boot ROM decides for itself)
        } else if (std::strcmp(argv[i], "--no-idle-skip") == 0) {
            idleSkip = false;
        } else if (std::strcmp(argv[i], "--speed") == 0) {
            showSpeed = true;
        } else if (std::strcmp(argv[i], "--two-players") == 0) {
            twoPlayers = true;
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            maxTicks = std::atol(argv[++i]);
        } else if (std::strcmp(argv[i], "--record") == 0 && i + 1 < argc) {
            recordPath = argv[++i];
        } else if (std::strcmp(argv[i], "--play") == 0 && i + 1 < argc) {
            playPath = argv[++i];
        } else if (std::strcmp(argv[i], "--author") == 0 && i + 1 < argc) {
            author = argv[++i];
        } else if (std::strcmp(argv[i], "--roms") == 0 && i + 1 < argc) {
            romDirs.push_back(argv[++i]);
        } else if (std::strcmp(argv[i], "--swap-rom") == 0 && i + 1 < argc) {
            swapRoms.push_back(argv[++i]);
        } else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            scale = std::atoi(argv[++i]);
            if (scale <= 0) {
                std::cerr << "Invalid --scale value, using 2x\n";
                scale = 2;
            }
        } else if (std::strcmp(argv[i], "--boot") == 0 && i + 1 < argc) {
            bootPath = argv[++i];
        } else if (std::strcmp(argv[i], "--apu-bios") == 0 && i + 1 < argc) {
            apuBiosPath = argv[++i];
        } else if (argv[i][0] != '-') {
            romPath = argv[i];
        }
    }

    Window window(scale);
    if (!window.init()) {
        std::cerr << "Failed to initialize window\n";
        return 1;
    }

    // No ROM and no alternate Boot ROM/APU BIOS: fall back to the static test pattern.
    if (romPath.empty() && bootPath.empty() && apuBiosPath.empty() && playPath.empty()) {
        return runTestPattern(window);
    }

    // Full-system mode: drive CPU, PPU, APU, DMA, and display through System.
    System system;
    system.setVideoThread(System::videoThreadDefault());   // PPU/display catch-up beside the CPU (2+ cores)
    system.setIdlePollSkip(idleSkip);
    if (devMode) {
        system.setDevMode(true);
    }
    system.setVerifySignatures(!noVerify && System::verifySignaturesDefault());
    if (devUnit) system.setDevUnit(true);   // before any boot: the Boot ROM reads it. (A movie being played sets its own.)
    if (!devUnit && (!serialOuts.empty() || !serialIns.empty() || serialTcpPort >= 0))
        std::cerr << "note: --serial-* only does anything with --dev-unit (or a dev-unit movie)\n";
    // Movies set the machine up themselves (the pinned boot ROM, APU BIOS and cartridge), so a recording replays from
    // exactly the state it started in; dev mode isn't part of that state, so it can't be combined with them.
    const bool movieMode = !playPath.empty() || !recordPath.empty();
    if (movieMode && devMode) {
        std::cerr << "--dev can't be combined with --record/--play (it is not part of a movie's setup)\n";
        return 1;
    }
    if (!playPath.empty() && !recordPath.empty()) {
        std::cerr << "--record and --play are exclusive\n";
        return 1;
    }
    RomLibrary library;
    Movie playMovie;
    MovieSession movieSession;
    std::string movieError;
    if (!playPath.empty()) {
        if (!Movie::load(playPath, playMovie, movieError)) {
            std::cerr << movieError << "\n";
            return 1;
        }
        const std::string dir = std::filesystem::path(playPath).parent_path().string();
        library.add(dir.empty() ? "." : dir);
        for (const std::string& r : romDirs) library.add(r);
        for (const std::string& r : swapRoms) library.add(r);
        if (!movieSession.beginPlayback(system, playMovie, library, movieError)) {
            std::cerr << movieError << "\n";
            return 1;
        }
        std::cout << "Playing " << playPath << " (" << playMovie.lengthCycles << " master cycles)...\n";
    } else if (!recordPath.empty()) {
        MovieSession::RecordSetup setup;
        setup.bootRomPath = bootPath;
        setup.apuBiosPath = apuBiosPath;
        setup.cartridgePath = romPath;
        setup.verifySignatures = !noVerify && System::verifySignaturesDefault();
        setup.devUnit = devUnit;
        setup.author = author;
        if (!movieSession.beginRecording(system, setup, movieError)) {
            std::cerr << movieError << "\n";
            return 1;
        }
        std::cout << "Recording to " << recordPath << " (ESC to stop and save)...\n";
    } else {
        if (!bootPath.empty() && !system.loadBootROM(bootPath)) {
            std::cerr << "Failed to load Boot ROM: " << bootPath << "\n";
            return 1;
        }
        if (!apuBiosPath.empty() && !system.loadAPUBios(apuBiosPath)) {
            std::cerr << "Failed to load APU BIOS: " << apuBiosPath << "\n";
            return 1;
        }
    }

    // The host end of the debug serial port, once we know whether this is a dev unit (a movie being played decides for itself).
    // Guest bytes go to the sink; host bytes are fed in a few per tick, only while the guest's queue has room, and through the
    // movie session when recording (it logs them at their exact cycle; a replay is in control and takes none).
    std::unique_ptr<SerialHost> serialHost;
    if (system.isDevUnit()) {
        serialHost.reset(new SerialHost);
        std::string err;
        bool ok = true;
        if (serialOuts.empty() && serialTcpPort < 0) serialOuts.push_back("-");   // a dev unit's console goes to the terminal by default
        for (const std::string& o : serialOuts) ok = serialHost->addOutput(o, err) && ok;
        for (const std::string& in : serialIns) ok = serialHost->addInput(in, err) && ok;
        if (serialTcpPort >= 0) {
            if (serialTcpPort > 65535) { err = "--serial-tcp wants a port number"; ok = false; }
            else ok = serialHost->listenTcp((uint16_t)serialTcpPort, err) && ok;
        }
        if (!ok) {
            std::cerr << err << "\n";
            return 1;
        }
        SerialHost* host = serialHost.get();
        system.setSerialSink([host](uint8_t b) { host->send(b); });
    }
    auto serialTick = [&]() {
        if (!serialHost) return;
        uint8_t b;
        for (int n = 0; n < 192 && system.serialRxHasRoom() && serialHost->receive(b); n++) {   // ~one 60 Hz tick of 115200 baud
            if (movieSession.mode() == MovieSession::Mode::Recording) movieSession.serialIn(b);
            else if (movieSession.mode() != MovieSession::Mode::Playing) system.serialReceive(b);
        }
        serialHost->flush();
    };

    // A Boot ROM payload with no cartridge (e.g. a hardware demo) runs on
    // its own - only load/require a ROM if one was actually given.
    if (romPath.empty() && !movieMode) {
        system.powerOn();
        std::cout << "Running Boot ROM demo (no cartridge)...\n";
        FramePacer pacer(FramePacer::AUDIO_LEAD);
        while (!window.shouldClose()) {
            auto frameStart = std::chrono::steady_clock::now();
            window.pollEvents();
            serialTick();
            const int due = pacer.framesDue(std::chrono::duration<double>(frameStart.time_since_epoch()).count());
            int done = 0;
            while (done < due) {
                system.getDisplay().setLatchEnabled(FramePacer::shouldLatch(
                    done, due, std::chrono::duration<double>(std::chrono::steady_clock::now() - frameStart).count()));
                system.stepFrame();
                done++;
                if (std::chrono::duration<double>(std::chrono::steady_clock::now() - frameStart).count() >=
                    FramePacer::STEP_BUDGET_SECONDS) break;   // a slow frame must not starve the window
            }
            pacer.stepped(done);
            window.render(system.getDisplay());

            const auto& audio = system.getAudioBuffer();
            window.queueAudio(audio.data(), static_cast<int>(audio.size() / 2));
            system.clearAudioBuffer();

            auto elapsed = std::chrono::steady_clock::now() - frameStart;
            if (elapsed < std::chrono::microseconds(16667)) {
                std::this_thread::sleep_for(std::chrono::microseconds(16667) - elapsed);
            }
        }
        return 0;
    }

    if (!movieMode) {
        if (!system.loadROM(romPath)) {
            std::cerr << "Failed to load ROM: " << romPath << "\n";
            return 1;
        }
        system.powerOn();
    }

    std::cout << "Running '" << system.getROMTitle() << "'...\n";

    auto readImage = [](const std::string& path, std::vector<uint8_t>& out) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return !out.empty();
    };
    size_t nextSwap = 0;
    const bool moviePlaying = movieSession.mode() == MovieSession::Mode::Playing;
    const bool movieRecording = movieSession.mode() == MovieSession::Mode::Recording;

    // Every ~60 Hz tick: step as many System frames as it takes for the console clock to catch up with the
    // wall clock (a System frame is ~1.3 ms, see frame_pacer.h), then render once. Sleep out the rest of the
    // tick so we don't spin the host CPU at 100%. (A movie runs for as long as its time lasts, not until the CPU
    // halts: a reset can bring a halted machine back.)
    const auto frameDuration = std::chrono::microseconds(16667);
    FramePacer pacer(FramePacer::AUDIO_LEAD);
    bool playbackEnded = false;
    long ticks = 0;
    // Speed readout, same maths as the Qt status bar: console seconds stepped per wall second, as "fps" out of 60.
    // Only printed with --speed (a plain run stays quiet).
    auto speedWindowStart = std::chrono::steady_clock::now();
    long speedFrames = 0;
    while (!window.shouldClose() && (movieMode || !system.getCPU().isHalted()) && !playbackEnded &&
           (maxTicks < 0 || ticks++ < maxTicks)) {
        auto frameStart = std::chrono::steady_clock::now();

        window.pollEvents();

        uint8_t direction, control, buttons, direction2, control2, buttons2;
        window.getPlayerInput(direction, control, buttons);
        window.getPlayer2Input(direction2, control2, buttons2, twoPlayers);
        const unsigned hotkeys = window.takeHotkeys();
        serialTick();

        if (moviePlaying) {
            // The movie is in control: keys do nothing but ESC.
        } else if (movieRecording) {
            // Logged at the exact movie cycle, then applied: the recording and its replay run the same code.
            movieSession.input(0, direction, control, buttons);
            movieSession.input(1, direction2, control2, buttons2);
            if (hotkeys & Window::HOTKEY_SOFT_RESET) movieSession.softReset();
            if (hotkeys & Window::HOTKEY_HARD_RESET) movieSession.hardReset();
            if (hotkeys & Window::HOTKEY_YANK) movieSession.yank();
            if ((hotkeys & Window::HOTKEY_SWAP) && !swapRoms.empty()) {
                std::vector<uint8_t> image;
                const std::string& path = swapRoms[nextSwap++ % swapRoms.size()];
                if (!readImage(path, image) || !movieSession.insert(image, std::filesystem::path(path).filename().string()))
                    std::cerr << "Can't swap in " << path << "\n";
            }
        } else {
            system.getCPU().setPlayerInput(direction, control, buttons);
            system.getCPU().setPlayer2Input(direction2, control2, buttons2);
            if (hotkeys & Window::HOTKEY_SOFT_RESET) system.softReset();
            if (hotkeys & Window::HOTKEY_HARD_RESET) system.hardReset();
            if (hotkeys & Window::HOTKEY_YANK) system.ejectCartridge();
            if ((hotkeys & Window::HOTKEY_SWAP) && !swapRoms.empty()) {
                std::vector<uint8_t> image;
                const std::string& path = swapRoms[nextSwap++ % swapRoms.size()];
                if (!readImage(path, image) || !system.insertCartridge(image)) std::cerr << "Can't swap in " << path << "\n";
            }
        }

        const int due = pacer.framesDue(std::chrono::duration<double>(frameStart.time_since_epoch()).count());
        int done = 0;
        while (done < due && (movieMode || !system.getCPU().isHalted())) {
            system.getDisplay().setLatchEnabled(FramePacer::shouldLatch(
                done, due, std::chrono::duration<double>(std::chrono::steady_clock::now() - frameStart).count()));
            if (movieMode) {
                if (movieSession.run(System::CYCLES_PER_FRAME) == 0 && moviePlaying) break;   // playback is over (or stopped)
            } else {
                system.stepFrame();
            }
            done++;
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - frameStart).count() >=
                FramePacer::STEP_BUDGET_SECONDS) break;   // a slow frame must not starve the window
        }
        pacer.stepped(done);
        speedFrames += done;
        if (showSpeed) {
            const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - speedWindowStart).count();
            if (wall >= 1.0) {
                const double speed = speedFrames * FramePacer::FRAME_SECONDS / wall;
                std::cout << "speed: " << static_cast<int>(speed * 100.0 + 0.5) << "% (" << static_cast<int>(speed * 60.0 + 0.5)
                          << " fps)\n" << std::flush;
                speedWindowStart = std::chrono::steady_clock::now();
                speedFrames = 0;
            }
        }

        if (moviePlaying && (movieSession.finished() || movieSession.desynced() || !movieSession.failure().empty())) playbackEnded = true;

        window.render(system.getDisplay());

        const auto& audio = system.getAudioBuffer();
        window.queueAudio(audio.data(), static_cast<int>(audio.size() / 2));
        system.clearAudioBuffer();

        auto elapsed = std::chrono::steady_clock::now() - frameStart;
        if (elapsed < frameDuration) {
            std::this_thread::sleep_for(frameDuration - elapsed);
        }
    }

    int exitCode = 0;
    if (moviePlaying) {
        if (!movieSession.failure().empty()) {
            std::cerr << "Playback failed: " << movieSession.failure() << "\n";
            exitCode = 1;
        } else if (movieSession.desynced()) {
            std::cerr << movieSession.desyncReport() << "\n";
            exitCode = 1;
        } else if (movieSession.finished()) {
            std::cout << "Movie finished: " << movieSession.checkpointsVerified() << " checkpoints verified.\n";
        }
    } else if (movieRecording) {
        const Movie recorded = movieSession.finishRecording();
        std::string saveError;
        if (recorded.save(recordPath, saveError)) {
            std::cout << "Saved " << recordPath << ": " << recorded.events.size() << " events over " << recorded.lengthCycles
                      << " master cycles.\n";
        } else {
            std::cerr << saveError << "\n";
            exitCode = 1;
        }
    }

    if (!movieMode && system.getCPU().isHalted()) {
        std::cout << "CPU halted after " << system.getMasterCycleCount()
                  << " master cycles.\n";
    }
    std::cout << "Shutting down...\n";
    return exitCode;
}
