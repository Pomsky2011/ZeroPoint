// High-level-emulated APU BIOS: a native stand-in for the internal BIOS
// (ZPbootROM apu/internalBIOS launch.uas + engine.uas), used when no BIOS image
// is loaded. See the comment on APU::enableHLEBios() in apu.h for when it runs.
//
// It must be indistinguishable from the real BIOS where software can tell: the
// header/checksum/magic protocol, the mailbox ($1000) and status ($1800) bytes,
// the engine tick counter at $0207, and above all the MMP register writes of
// every tick. tools/test_zpa_engine diffs those against the assembly engine and
// against the zpsnd reference interpreter (ZPdevtools docs/apu/zpa.txt).
#include "apu.h"

namespace {

constexpr uint16_t HEADER = 0x3000;         // +0 magic, +4/+5 entry, +$1f checksum
constexpr uint16_t SCORE_BASE = 0x3020;     // score offsets (JUMP, JZ, JEQ) are relative to this
constexpr uint16_t SCORE_END = 0x6000;      // the checksum covers $3020-$5FFF
constexpr uint16_t MAILBOX = 0x1000;        // CPU -> APU result byte
constexpr uint16_t STATUS = 0x1800;         // APU -> CPU byte written by POST
constexpr uint16_t TICK_COUNTER = 0x0207;   // 16-bit engine tick counter (little-endian)

constexpr uint8_t MAGIC_PROGRAM = 0x0A;
constexpr uint8_t MAGIC_SCORE = 0x0B;

constexpr uint64_t APU_HZ = 4250710;
constexpr uint64_t TICK_HZ = 120;

// What the real BIOS spends before the first tick, measured with
// tools/test_zpa_engine --timing: the header poll plus the 12,256-byte checksum
// scan, then engine_start's channel setup.
constexpr uint64_t POLL_CYCLES = 24;
constexpr uint64_t SCAN_CYCLES = 589328;
constexpr uint64_t ENGINE_INIT_CYCLES = 8000;

enum Op : uint8_t {
    OP_END = 0x00, OP_NOTE = 0x01, OP_PITCH = 0x02, OP_VOL = 0x03, OP_PAN = 0x04, OP_OFF = 0x05,
    OP_SET = 0x06, OP_POST = 0x07, OP_NOP = 0x08, OP_MARK = 0x10, OP_REP = 0x11, OP_JUMP = 0x12,
    OP_JZ = 0x13, OP_JEQ = 0x14, OP_WAIT = 0x15,
};

uint16_t clamp16(int v) { return v < 0 ? 0 : (v > 0xFFFF ? 0xFFFF : (uint16_t)v); }

}  // namespace

void APU::enableHLEBios(bool on) {
    hleBios = on;
    hleMode = HleMode::None;
}

bool APU::hleIntercept() {
    // The HLE never moves PC, so a different PC means someone (loadAPUProgram,
    // a debugger) redirected the APU: stop impersonating the BIOS.
    if (hleMode != HleMode::None && pc != BIOS_BASE) hleMode = HleMode::None;

    if (hleMode == HleMode::None) {
        // Only an empty BIOS entry is ours: code loaded at $8000 is a program.
        if (pc != BIOS_BASE || memory[BIOS_BASE] != 0 || memory[BIOS_BASE + 1] != 0) return false;
        hleMode = HleMode::WaitHeader;
    }

    if (hleMode == HleMode::WaitHeader) {
        hleWaitForHeader();
        return true;
    }

    // Engine: idle until the next tick is due (the real engine stalls), then run it.
    const uint64_t due = hle.startCycle + (uint64_t)hle.tick * APU_HZ / TICK_HZ;
    if (cycleCount < due) cycleCount = due;
    hleTick();
    return true;
}

// The BIOS idle loop: poll the header magic, and when one shows up verify the
// checksum (a bad one is simply re-scanned, like the real BIOS does).
void APU::hleWaitForHeader() {
    const uint8_t magic = memory[HEADER];
    if (magic != MAGIC_PROGRAM && magic != MAGIC_SCORE) {
        cycleCount += POLL_CYCLES;
        instructionCount += POLL_CYCLES / 4;
        return;
    }

    uint8_t sum = 0;
    for (uint32_t a = SCORE_BASE; a < SCORE_END; a++) sum += memory[a];
    cycleCount += SCAN_CYCLES;
    instructionCount += SCAN_CYCLES / 4;
    if (sum != memory[HEADER + 0x1F]) return;

    // Accepted: zero the magic before running anything, so a program or score
    // that returns here must wait for a NEW header (ZOA clears two bytes).
    memory[HEADER] = 0;
    memory[HEADER + 1] = 0;
    const uint16_t entry = memory[HEADER + 4] | (memory[HEADER + 5] << 8);

    if (magic == MAGIC_PROGRAM) {
        // The real BIOS leaves RP=$80, DP=$30, SP=$0100 and X/Y = the entry point.
        hleMode = HleMode::None;
        rp = 0x80;
        dp = 0x30;
        sp = 0x0100;
        registers[0] = entry & 0xFF;
        registers[1] = entry >> 8;
        pc = entry;
        return;
    }
    hleStartEngine(entry);
}

void APU::hleStartEngine(uint16_t entry) {
    hle = HleEngine();
    hle.ptr = entry;
    hle.startCycle = cycleCount + ENGINE_INIT_CYCLES;
    memory[TICK_COUNTER] = 0;
    memory[TICK_COUNTER + 1] = 0;
    hleReadDelta();
    hleMode = HleMode::Engine;
}

uint8_t APU::hleFetch() { return memory[hle.ptr++]; }

// CNT = the delta of the event at the pointer (one byte, or $80|hi then lo).
void APU::hleReadDelta() {
    const uint8_t b = hleFetch();
    if (b & 0x80) hle.cnt = ((b & 0x7F) << 8) | hleFetch();
    else hle.cnt = b;
}

void APU::hleWrite16(uint16_t reg, uint16_t value) {
    writeByte(reg, value & 0xFF);       // low first: the high write is what applies / restarts
    writeByte(reg + 1, value >> 8);
}

// Silence a channel's sample, and forget a pending start.
void APU::hleStop(int c) {
    HleChannel& ch = hle.ch[c];
    if (ch.stl || ch.pending) hleWrite16(0x50 + 2 * c, 0);
    ch.stl = ch.pending = 0;
}

// Run the event at the pointer. Returns false on END (every channel silenced).
bool APU::hleRunEvent() {
    const uint8_t op = hleFetch();
    uint16_t jump = 0;
    bool jumped = false;

    switch (op) {
    case OP_NOTE: {
        const int c = hleFetch() & 15;
        const uint8_t idx = hleFetch();
        const uint16_t pitch = (hleFetch() << 8);
        const uint16_t pitchLo = hleFetch();
        const uint8_t vol = hleFetch(), pan = hleFetch();
        HleChannel& ch = hle.ch[c];
        for (int i = 0; i < 3; i++) ch.slideTicks[i] = 0;
        ch.pitch = pitch | pitchLo;
        ch.vol = vol << 8;
        ch.pan = pan << 8;
        ch.live = true;
        ch.force = true;
        if (ch.stl) hleWrite16(0x50 + 2 * c, 0);    // a same-address write would not retrigger
        ch.stl = 0;
        ch.pending = STL_BASE + idx * 4;
        break;
    }
    case OP_SET: {
        const int c = hleFetch() & 15;
        HleChannel& ch = hle.ch[c];
        const uint16_t pitch = (hleFetch() << 8);
        ch.pitch = pitch | hleFetch();
        const uint8_t vol = hleFetch(), pan = hleFetch();
        for (int i = 0; i < 3; i++) ch.slideTicks[i] = 0;
        ch.vol = vol << 8;
        ch.pan = pan << 8;
        ch.live = true;
        break;
    }
    case OP_PITCH:
    case OP_VOL:
    case OP_PAN: {
        const int c = hleFetch() & 15;
        const uint16_t hi = hleFetch() << 8;
        const int16_t delta = (int16_t)(hi | hleFetch());
        const uint8_t ticks = hleFetch();
        const int k = op - OP_PITCH;
        hle.ch[c].slideDelta[k] = delta;
        hle.ch[c].slideTicks[k] = ticks;
        break;
    }
    case OP_OFF: {
        const int c = hleFetch() & 15;
        for (int i = 0; i < 3; i++) hle.ch[c].slideTicks[i] = 0;
        hleStop(c);
        break;
    }
    case OP_POST:
        writeByte(STATUS, hleFetch());
        break;
    case OP_NOP:
        break;
    case OP_MARK:
        if (hle.depth < 4) hle.frames[hle.depth++] = {hle.ptr, 0, false};
        break;
    case OP_REP: {
        const uint8_t count = hleFetch();
        if (hle.depth == 0) break;
        auto& f = hle.frames[hle.depth - 1];
        if (!f.started) {
            f.started = true;
            f.count = count;
        }
        if (f.count > 0) {
            f.count--;
            jump = f.ptr;
            jumped = true;
        } else {
            hle.depth--;
        }
        break;
    }
    case OP_JUMP: {
        const uint16_t hi = hleFetch() << 8;
        jump = SCORE_BASE + (hi | hleFetch());
        jumped = true;
        break;
    }
    case OP_JZ: {
        const uint16_t hi = hleFetch() << 8;
        const uint16_t target = SCORE_BASE + (hi | hleFetch());
        if (memory[MAILBOX] == 0) { jump = target; jumped = true; }
        break;
    }
    case OP_JEQ: {
        const uint8_t val = hleFetch();
        const uint16_t hi = hleFetch() << 8;
        const uint16_t target = SCORE_BASE + (hi | hleFetch());
        if (memory[MAILBOX] == val) { jump = target; jumped = true; }
        break;
    }
    case OP_WAIT:
        hle.waiting = memory[MAILBOX] == 0;
        break;
    default:                                    // END, and anything unknown
        for (int c = 0; c < 16; c++) hleStop(c);
        return false;
    }

    if (jumped) hle.ptr = jump;
    hleReadDelta();                             // the next event's delta counts from now (or the jump)
    return true;
}

void APU::hleTick() {
    if (hle.waiting && memory[MAILBOX] != 0) hle.waiting = false;

    while (!hle.waiting && hle.cnt == 0) {
        if (!hleRunEvent()) {
            hleMode = HleMode::WaitHeader;      // END: back to the BIOS idle loop (tick counter stays)
            return;
        }
    }

    for (int c = 0; c < 16; c++) {
        HleChannel& ch = hle.ch[c];
        uint16_t* value[3] = {&ch.pitch, &ch.vol, &ch.pan};
        for (int k = 0; k < 3; k++) {
            if (ch.slideTicks[k] > 0) {
                ch.slideTicks[k]--;
                *value[k] = clamp16((int)*value[k] + ch.slideDelta[k]);
            }
        }
        if (!ch.live) continue;

        if (ch.force || ch.wPitch != ch.pitch) {
            ch.wPitch = ch.pitch;
            hleWrite16(2 * c, ch.pitch);
        }
        const uint8_t vol = ch.vol >> 8, pan = ch.pan >> 8;
        if (ch.force || ch.wVol != vol) {
            ch.wVol = vol;
            writeByte(0x20 + c, vol);
        }
        if (ch.force || ch.wPan != pan) {
            ch.wPan = pan;
            writeByte(0x30 + c, pan);
        }
        ch.force = false;
        if (ch.pending) {
            ch.stl = ch.pending;
            ch.pending = 0;
            hleWrite16(0x50 + 2 * c, ch.stl);
        }
    }

    if (!hle.waiting && hle.cnt > 0) hle.cnt--;

    hle.tick++;
    memory[TICK_COUNTER] = hle.tick & 0xFF;
    memory[TICK_COUNTER + 1] = (hle.tick >> 8) & 0xFF;
}
