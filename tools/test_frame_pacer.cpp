// test_frame_pacer: FramePacer keeps the console clock on the wall clock, caps catch-up, and never goes backwards.
#include "frame_pacer.h"
#include <cstdio>

using namespace ZeroPoint;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

int main() {
    // 10 s of 60 Hz ticks on a healthy host: the console clock lands within one frame of the wall clock.
    {
        FramePacer p;
        long frames = 0;
        for (int i = 0; i <= 600; i++) { int n = p.framesDue(100.0 + i / 60.0); p.stepped(n); frames += n; }
        const double console = frames * FramePacer::FRAME_SECONDS;
        std::printf("  (%ld frames = %.4f s of console time for 10 s of wall time, %.1f frames/tick)\n", frames, console, frames / 601.0);
        check(console > 9.99 && console <= 10.0 + 1e-9, "console time tracks wall time (10 s -> within one frame)");
        check(frames / 601.0 > 12 && frames / 601.0 < 14, "about 13 System frames per 60 Hz tick");
    }
    // A 2 s stall (window drag, slow host): at most MAX_LAG of it is replayed, in capped batches.
    {
        FramePacer p;
        p.framesDue(0.0);
        int n = p.framesDue(2.0);
        p.stepped(n);
        check(n == FramePacer::MAX_FRAMES_PER_TICK, "a long stall is stepped in a capped batch");
        double total = n * FramePacer::FRAME_SECONDS;
        double t = 2.0;
        for (int i = 0; i < 60; i++) { t += 1.0 / 60; int k = p.framesDue(t); p.stepped(k); total += k * FramePacer::FRAME_SECONDS; }
        check(total < 1.25 + FramePacer::MAX_LAG + 0.05, "the stall is not chased in full (dropped past MAX_LAG)");
        // ...and it is back in step afterwards.
        double after = 0;
        for (int i = 0; i < 60; i++) { t += 1.0 / 60; int k = p.framesDue(t); p.stepped(k); after += k * FramePacer::FRAME_SECONDS; }
        check(after > 0.98 && after < 1.02, "back to one console second per wall second after the stall");
    }
    // A host too slow to keep up (it steps only 4 of the frames due each tick): the debt stays bounded by MAX_LAG,
    // and the console still advances at the host's own speed instead of the pacer demanding more.
    {
        FramePacer p;
        int worst = 0;
        long done = 0;
        for (int i = 0; i <= 600; i++) {
            int n = p.framesDue(i / 60.0);
            worst = n > worst ? n : worst;
            int k = n < 4 ? n : 4;
            p.stepped(k);
            done += k;
        }
        check(worst <= (int)(FramePacer::MAX_LAG / FramePacer::FRAME_SECONDS) + 2, "a slow host's backlog never exceeds MAX_LAG");
        check(done == 4L * 601 || done > 4L * 590, "and it still advances at the host's own pace");
    }
    // A clock that doesn't move (or goes backwards) steps nothing.
    {
        FramePacer p;
        p.stepped(p.framesDue(5.0));
        check(p.framesDue(5.0) == 0 && p.framesDue(4.0) == 0, "no time passing (or time going back) steps no frames");
    }
    // Latch policy (Display::setLatchEnabled): a tick that stops early on its step budget must still have latched
    // a raster, or a host slower than real time would never show anything (a black screen).
    {
        // 16 rasters due; each costs 1.5 ms, so the 12 ms budget stops the tick after 8.
        const double cost = 0.0015;
        int done = 0, latched = 0, lastLatchedAt = -1;
        const int due = 16;
        while (done < due) {
            if (FramePacer::shouldLatch(done, due, done * cost)) { latched++; lastLatchedAt = done; }
            done++;
            if (done * cost >= FramePacer::STEP_BUDGET_SECONDS) break;
        }
        check(latched >= 1, "a budget-limited tick latches at least one raster");
        check(lastLatchedAt >= done - 2, "...and the last latched raster is at (or one before) where the tick stopped");
        check(latched <= 3, "...but only a few, not every raster");
        // Healthy host: the whole batch fits the budget, the last raster is latched.
        bool lastOne = FramePacer::shouldLatch(12, 13, 0.0005);
        check(lastOne && !FramePacer::shouldLatch(5, 13, 0.0005), "a batch that fits latches the first and last raster only");
    }
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
