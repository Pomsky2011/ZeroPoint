// Regression test for the MMP stream port ($0070-$007F): a stereo 8-bit PCM ring the CPU/DMA fills and the mixer
// drains. Drives the APU the way System::tickComponents does (updateMMP, then getMixedSample*) and checks gapless
// playback across ring wraps, underrun behaviour, register semantics, resampling, volume and mixing.
#include "apu.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

static const uint16_t BASE = 0x3000;

static void w16(APU& apu, uint16_t reg, uint16_t v) {   // low byte first: 16-bit regs apply on the high write
    apu.writeByte(reg, v & 0xFF);
    apu.writeByte(reg + 1, v >> 8);
}
static uint16_t r16(APU& apu, uint16_t reg) { return apu.readByte(reg) | (apu.readByte(reg + 1) << 8); }

static void setup(APU& apu, uint16_t size, uint16_t pitch = 0x1000) {
    w16(apu, 0x72, BASE);
    w16(apu, 0x74, size);
    apu.writeByte(0x70, 0x02);   // RESET
    w16(apu, 0x7A, pitch);
    apu.writeByte(0x70, 0x01);   // ENABLE
}

static void putFrame(APU& apu, uint16_t idx, int l, int r) {
    apu.writeByte(BASE + idx * 2, static_cast<uint8_t>(l));
    apu.writeByte(BASE + idx * 2 + 1, static_cast<uint8_t>(r));
}

struct LR { int l, r; };
static LR tick(APU& apu) {
    apu.updateMMP();
    return {apu.getMixedSampleLeft(), apu.getMixedSampleRight()};
}

int main() {
    std::printf("plays frames in order, then goes silent + UNDERRUN when the ring runs dry\n");
    {
        APU apu;
        setup(apu, 16);
        for (int i = 0; i < 12; i++) putFrame(apu, i, 10 * (i + 1), -10 * (i + 1));
        check(tick(apu).l == 0, "nothing plays before WRPTR is committed");
        check((apu.readByte(0x71) & 1) != 0, "UNDERRUN set when the ring is empty");
        apu.writeByte(0x71, 0x01);
        check((apu.readByte(0x71) & 1) == 0, "writing 1 to STATUS bit 0 clears UNDERRUN");
        apu.writeByte(0x78, 12);
        check(tick(apu).l == 0 && r16(apu, 0x76) == 0, "WRPTR low-byte write alone does not commit (still starved)");
        apu.writeByte(0x71, 0x01);   // that starved tick re-set the sticky flag
        apu.writeByte(0x79, 0);
        check(r16(apu, 0x78) == 12, "WRPTR commits on the high-byte write");
        bool ok = true;
        for (int i = 0; i < 10; i++) {
            LR o = tick(apu);
            if (o.l != 10 * (i + 1) * 256 || o.r != -10 * (i + 1) * 256) ok = false;
        }
        check(ok, "frames 0-9 come out exactly (3-frame lookahead leaves 10 and 11)");
        check(r16(apu, 0x76) == 10, "RDPTR advanced to 10");
        check((apu.readByte(0x71) & 2) != 0 && (apu.readByte(0x71) & 1) == 0, "PLAYING, no underrun yet");
        LR o = tick(apu);
        check(o.l == 0 && o.r == 0, "starved tick is silent");
        check((apu.readByte(0x71) & 3) == 1, "UNDERRUN set, PLAYING clear");
        check(r16(apu, 0x76) == 10, "starved tick holds RDPTR");
        apu.writeByte(0x76, 3);
        check(r16(apu, 0x76) == 10, "RDPTR is read-only");
    }

    std::printf("a CPU that keeps the ring topped up gets gapless audio across many wraps\n");
    {
        APU apu;
        setup(apu, 8);
        uint16_t wr = 0, next = 0;
        auto feed = [&] {
            for (;;) {
                uint16_t rd = r16(apu, 0x76);
                uint16_t freeFrames = (rd + 8 - wr - 1) % 8;
                if (!freeFrames) break;
                putFrame(apu, wr, (next % 100) + 1, -((next % 100) + 1));
                next++;
                wr = (wr + 1) % 8;
                w16(apu, 0x78, wr);
            }
        };
        feed();
        std::vector<int> got;
        for (int t = 0; t < 400; t++) {
            feed();
            got.push_back(tick(apu).l / 256);
        }
        bool ok = true;
        for (size_t i = 0; i < got.size(); i++) if (got[i] != (int)(i % 100) + 1) ok = false;
        check(ok, "400 ticks over an 8-frame ring match the source sequence");
        check((apu.readByte(0x71) & 1) == 0, "no underrun");
    }

    std::printf("resampling by PITCH\n");
    {
        APU apu;
        setup(apu, 64, 0x0800);   // half speed
        for (int i = 0; i < 40; i++) putFrame(apu, i, 4 * i, 4 * i);   // linear ramp: cubic is exact
        w16(apu, 0x78, 40);
        bool ok = true;
        for (int t = 0; t < 40; t++) {
            int expect = (int)(4 * (t * 0.5) * 256);
            if (std::abs(tick(apu).l - expect) > 256) ok = false;
        }
        check(ok, "0x0800 interpolates a ramp at half rate");

        APU b;
        setup(b, 64, 0x2000);   // double speed
        for (int i = 0; i < 40; i++) putFrame(b, i, 3 * i, 3 * i);
        w16(b, 0x78, 40);
        ok = true;
        for (int t = 0; t < 15; t++)
            if (tick(b).l != 3 * (2 * t) * 256) ok = false;
        check(ok, "0x2000 plays every other frame");
    }

    std::printf("volume, enable, reset and mixing with the 16 channels\n");
    {
        APU apu;
        setup(apu, 16);
        for (int i = 0; i < 12; i++) putFrame(apu, i, 100, -100);
        w16(apu, 0x78, 12);
        apu.writeByte(0x7C, 128);
        LR o = tick(apu);
        check(o.l == 100 * 256 * 128 / 255 && o.r == -100 * 256 * 128 / 255, "VOL scales both channels");
        apu.writeByte(0x70, 0x00);
        o = tick(apu);
        check(o.l == 0 && o.r == 0 && r16(apu, 0x76) == 1, "disabled: silent and RDPTR holds");
        apu.writeByte(0x70, 0x02);
        check(r16(apu, 0x76) == 0 && r16(apu, 0x78) == 0 && (apu.readByte(0x70) & 2) == 0, "RESET empties the ring and self-clears");
    }
    {
        APU apu;
        // channel 0: constant +5, full right, plus the stream's -10 on the right
        for (int i = 0; i < 12; i++) apu.writeByte(0x9000 + 4 + i, 5);
        apu.writeByte(0x9000 + 1, 0x04);
        apu.writeByte(0x7000, 0x00); apu.writeByte(0x7001, 0x90);
        apu.writeByte(0x7002, 0x00); apu.writeByte(0x7003, 0x00);
        w16(apu, 0x00, 0x1000);
        apu.writeByte(0x20, 255);
        apu.writeByte(0x30, 255);
        w16(apu, 0x50, 0x7000);
        setup(apu, 16);
        for (int i = 0; i < 12; i++) putFrame(apu, i, 0, -10);
        w16(apu, 0x78, 12);
        LR o = tick(apu);
        check(o.r == -5 * 256, "stream and a sample channel sum in the mixer");
    }

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
