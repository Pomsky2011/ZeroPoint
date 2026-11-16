// test_frame_pacer: FramePacer keeps the console clock on the wall clock, caps catch-up, and never goes backwards.
#include "frame_pacer.h"
#include <algorithm>
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

    // The audio lead: the console runs `lead` seconds ahead of the wall clock, so the device queue holds a buffer.
    {
        const double lead = FramePacer::AUDIO_LEAD;
        FramePacer p(lead);
        const int first = p.framesDue(100.0);
        check(first == static_cast<int>(lead / FramePacer::FRAME_SECONDS), "the first tick steps the lead's worth of frames");
        p.stepped(first);
        double console = first * FramePacer::FRAME_SECONDS, worstBehind = 1e9, worstAhead = -1e9;
        for (int i = 1; i <= 600; i++) {
            const double wall = i / 60.0;
            const int n = p.framesDue(100.0 + wall);
            p.stepped(n);
            console += n * FramePacer::FRAME_SECONDS;
            worstBehind = std::min(worstBehind, console - wall);
            worstAhead = std::max(worstAhead, console - wall);
        }
        check(worstBehind > lead - 2 * FramePacer::FRAME_SECONDS, "in steady state the console stays at least ~one lead ahead of the wall clock");
        check(worstAhead < lead + 0.001, "...and no further than that");

        // a host that stalls for a second is not chased in full, lead or not
        double t = 100.0 + 10.0 + 1.0;
        int n = p.framesDue(t);
        check(n == FramePacer::MAX_FRAMES_PER_TICK, "a long stall is still stepped in a capped batch with a lead");
        p.stepped(n);
        for (int i = 0; i < 60; i++) { t += 1.0 / 60; p.stepped(p.framesDue(t)); }   // the capped catch-up drains
        double after = 0;
        for (int i = 0; i < 60; i++) { t += 1.0 / 60; int k = p.framesDue(t); p.stepped(k); after += k * FramePacer::FRAME_SECONDS; }
        check(after > 0.98 && after < 1.02, "and it is back to one console second per wall second afterwards");

        // the default is no lead: nothing changed for callers that don't ask for one
        FramePacer plain;
        check(plain.framesDue(5.0) == 0, "a pacer built without a lead steps no frames on its first tick");
    }

    // What the lead is for: audio generated minus wall time elapsed is the device queue's depth, so negative at a
    // tick means the speakers got silence. A 50 ms host stall (three skipped 60 Hz ticks) runs a level pacer dry and
    // is absorbed by one with the lead; a stall past the lead still runs dry.
    {
        auto underruns = [](double lead, int stallTicks) {
            FramePacer p(lead);
            double generated = 0;
            int dry = 0;
            for (int i = 0; i <= 1200; i++) {
                if (i >= 600 && i < 600 + stallTicks) continue;   // the host doesn't run the emulator thread
                const double wall = i / 60.0;
                if (i > 120 && generated < wall - 1e-9) dry++;
                const int n = p.framesDue(100.0 + wall);
                p.stepped(n);
                generated += n * FramePacer::FRAME_SECONDS;
            }
            return dry;
        };
        check(underruns(0.0, 3) > 0, "without a lead a 50 ms stall runs the audio queue dry");
        check(underruns(FramePacer::AUDIO_LEAD, 3) == 0, "with the lead the same stall is absorbed");
        check(underruns(FramePacer::AUDIO_LEAD, 8) > 0, "a stall longer than the lead (130 ms) still does");
    }
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
