#pragma once
// FramePacer: how many System frames a frontend should step on this tick so the console's clock keeps up
// with the wall clock.
//
// A System frame is one raster (TOTAL_SCANLINES x LINE_CYCLES master cycles) and, since core revision 13, exactly 1/60 s of
// console time. (Before that a frame was ~1.3 ms and a frontend had to step ~13 per tick to keep real time.) A tick still
// asks framesDue(now), steps up to that many (usually 1, sometimes 0 or 2 as the clocks drift, or a catch-up burst after a
// stall), and reports how many it really stepped with stepped(n).
//
// A frame can cost more host time than console time (the boot ROM's hashing, a busy PPU program), so the
// frontend must also stop stepping once it has used its slice of the tick (STEP_BUDGET_SECONDS) - otherwise
// catching up starves the window. The unstepped remainder stays as lag; past MAX_LAG seconds the console
// clock is dropped forward instead of chased forever, so a slow host just runs slow and stays responsive.
//
// Audio: a frontend that pushes samples as it steps only ever holds about one tick's worth in the device queue, so any
// host stall longer than that (plus the device's own buffer) runs it dry and the speakers play silence: a click, or
// crackle if it keeps happening. Construct the pacer with a lead and the console runs that many seconds ahead of the
// wall clock instead of level with it, so the queue holds a real buffer. The price is the same lead of latency
// between what the console does and what you hear (the picture is not delayed).
#include "system.h"

namespace ZeroPoint {

class FramePacer {
public:
    static constexpr double FRAME_SECONDS =
        static_cast<double>(System::CYCLES_PER_FRAME) / static_cast<double>(System::TIMER_SECOND_PERIOD);
    static constexpr double MAX_LAG = 0.25;     // seconds of console time we will try to catch up
    static constexpr int MAX_FRAMES_PER_TICK = 32;   // above MAX_LAG's worth of frames (15), so it only bounds a pathological lead
    // Wall-clock time a frontend may spend stepping within one ~16.7 ms tick, leaving the rest for input,
    // drawing and the host's event loop.
    static constexpr double STEP_BUDGET_SECONDS = 0.012;
    // The lead the frontends use: the device queue then holds ~80 ms instead of one tick's worth. Less than MAX_LAG.
    static constexpr double AUDIO_LEAD = 0.08;

    explicit FramePacer(double leadSeconds = 0.0) : lead(leadSeconds) {}

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
        int n = static_cast<int>((lag + lead) / FRAME_SECONDS);
        if (n > MAX_FRAMES_PER_TICK) n = MAX_FRAMES_PER_TICK;
        if (n < 0) n = 0;
        return n;
    }

    void stepped(int frames) { console += frames * FRAME_SECONDS; }

    // Whether the raster about to be stepped should be latched into the frame the frontend shows (see
    // Display::setLatchEnabled): `done` of `due` rasters are stepped so far this tick, `elapsed` seconds in. Only
    // one raster per tick is ever shown, but a tick that runs out of STEP_BUDGET_SECONDS stops early, so "the
    // last one due" alone would never come on a host that is too slow, and the frame would stay black. So also
    // latch the first raster (there is always a recent whole frame) and the one after which the average raster
    // cost says the budget runs out.
    static bool shouldLatch(int done, int due, double elapsed) {
        if (done == 0 || done + 1 >= due) return true;
        return elapsed + elapsed / done >= STEP_BUDGET_SECONDS;
    }

    void reset() { started = false; console = 0; }

private:
    double lead = 0;        // seconds the console is held ahead of the wall clock (see the audio note above)
    bool started = false;
    double start = 0;
    double console = 0;     // console seconds stepped so far
};

}  // namespace ZeroPoint
