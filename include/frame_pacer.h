#pragma once
// FramePacer: how many System frames a frontend should step on this tick so the console's clock keeps up
// with the wall clock.
//
// A System frame is one raster (TOTAL_SCANLINES x TOTAL_PIXELS_PER_LINE master cycles, ~1.3 ms of console
// time), not 1/60 s, so a frontend that steps one frame per 60 Hz tick runs ~13x slow. Instead, each tick asks
// framesDue(now), steps up to that many, and reports how many it really stepped with stepped(n).
//
// A frame can cost more host time than console time (the boot ROM's hashing, a busy PPU program), so the
// frontend must also stop stepping once it has used its slice of the tick (STEP_BUDGET_SECONDS) - otherwise
// catching up starves the window. The unstepped remainder stays as lag; past MAX_LAG seconds the console
// clock is dropped forward instead of chased forever, so a slow host just runs slow and stays responsive.
#include "system.h"

namespace ZeroPoint {

class FramePacer {
public:
    static constexpr double FRAME_SECONDS =
        static_cast<double>(System::CYCLES_PER_FRAME) / static_cast<double>(System::TIMER_SECOND_PERIOD);
    static constexpr double MAX_LAG = 0.25;     // seconds of console time we will try to catch up
    static constexpr int MAX_FRAMES_PER_TICK = 128;
    // Wall-clock time a frontend may spend stepping within one ~16.7 ms tick, leaving the rest for input,
    // drawing and the host's event loop.
    static constexpr double STEP_BUDGET_SECONDS = 0.012;

    // How many frames the console clock is behind by. `now` is any monotonic clock in seconds.
    // Does not advance the console clock: call stepped() with what was actually stepped.
    int framesDue(double now) {
        if (!started) {
            started = true;
            start = now;
        }
        double lag = (now - start) - console;
        if (lag > MAX_LAG) {
            start += lag - MAX_LAG;
            lag = MAX_LAG;
        }
        int n = static_cast<int>(lag / FRAME_SECONDS);
        if (n > MAX_FRAMES_PER_TICK) n = MAX_FRAMES_PER_TICK;
        if (n < 0) n = 0;
        return n;
    }

    void stepped(int frames) { console += frames * FRAME_SECONDS; }

    void reset() { started = false; console = 0; }

private:
    bool started = false;
    double start = 0;
    double console = 0;     // console seconds stepped so far
};

}  // namespace ZeroPoint
