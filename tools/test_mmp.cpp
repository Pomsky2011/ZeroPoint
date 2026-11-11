// Regression test for the MMP sample-playback path: SST sector loading (final
// block flag, start address anywhere in SST), loop points and one-shots.
//
// Each case writes a sector straight into SST/STL, starts channel 0 at pitch
// $1000 (exactly one sample per output frame, so the cubic interpolator returns
// the stored sample untouched) fully panned right, and compares the stream the
// mixer produces against the samples it should have played.
#include "apu.h"
#include <cstdint>
#include <cstdio>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

// Writes one 16-byte SST block. byte1 = (Y << 4) | L with L = ZW00.
static void writeBlock(APU& apu, uint16_t addr, uint8_t byte1, const int8_t* samples) {
    apu.writeByte(addr + 0, 0x00);   // XX (unused by the mixer)
    apu.writeByte(addr + 1, byte1);
    apu.writeByte(addr + 2, 0x00);
    apu.writeByte(addr + 3, 0x00);
    for (int i = 0; i < 12; i++)
        apu.writeByte(addr + 4 + i, static_cast<uint8_t>(samples[i]));
}

static void writeStl(APU& apu, uint16_t stlAddr, uint16_t sampleAddr, uint16_t loopWord) {
    apu.writeByte(stlAddr + 0, sampleAddr & 0xFF);
    apu.writeByte(stlAddr + 1, sampleAddr >> 8);
    apu.writeByte(stlAddr + 2, loopWord & 0xFF);
    apu.writeByte(stlAddr + 3, loopWord >> 8);
}

// Starts channel 0 on the STL entry at stlAddr.
static void startChannel0(APU& apu, uint16_t stlAddr) {
    apu.writeByte(0x00, 0x00);       // pitch $1000
    apu.writeByte(0x01, 0x10);
    apu.writeByte(0x20, 255);        // volume
    apu.writeByte(0x30, 255);        // pan: full right, so right = sample * 256
    apu.writeByte(0x50, stlAddr & 0xFF);
    apu.writeByte(0x51, stlAddr >> 8);
}

// One output frame, the way System::tickComponents drives it.
static int nextRight(APU& apu) {
    apu.updateMMP();
    return apu.getMixedSampleRight();
}

static std::vector<int> run(APU& apu, int frames) {
    std::vector<int> out;
    for (int i = 0; i < frames; i++)
        out.push_back(nextRight(apu) / 256);
    return out;
}

static std::vector<int> expectRepeat(const std::vector<int>& cycle, int frames) {
    std::vector<int> out;
    for (int i = 0; i < frames; i++)
        out.push_back(cycle[i % cycle.size()]);
    return out;
}

static const int8_t kA[12] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120};
static const int8_t kB[12] = {-10, -20, -30, -40, -50, -60, -70, -80, -90, -100, -110, -120};
static const int8_t kC[12] = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};

int main() {
    std::vector<int> a(kA, kA + 12), b(kB, kB + 12);
    std::vector<int> ab = a;
    ab.insert(ab.end(), b.begin(), b.end());

    std::printf("final-block flag is L bit 2 (byte 1 & 0x04); sector ends there\n");
    {
        APU apu;
        writeBlock(apu, 0x9000, 0x04, kA);   // W set: sector is just this block
        writeBlock(apu, 0x9010, 0x04, kB);   // must never be played
        writeStl(apu, 0x7000, 0x9000, 0);
        startChannel0(apu, 0x7000);
        check(run(apu, 36) == expectRepeat(a, 36), "single-block sector loops itself, never reads on");
    }

    std::printf("multi-block sector ends at the W block, loop word 0 loops the whole sector\n");
    {
        APU apu;
        writeBlock(apu, 0x9000, 0x00, kA);
        writeBlock(apu, 0x9010, 0x04, kB);
        writeBlock(apu, 0x9020, 0x04, kC);   // belongs to another sector
        writeStl(apu, 0x7000, 0x9000, 0);
        startChannel0(apu, 0x7000);
        check(run(apu, 72) == expectRepeat(ab, 72), "A+B repeats with period 24");
    }

    std::printf("sector starting in the upper SST ($B000) loads\n");
    {
        APU apu;
        writeBlock(apu, 0xB000, 0x04, kA);
        writeStl(apu, 0x7000, 0xB000, 0);
        startChannel0(apu, 0x7000);
        check(run(apu, 24) == expectRepeat(a, 24), "block at $B000 plays");
    }
    {
        APU apu;
        writeBlock(apu, 0xCFF0, 0x04, kA);   // last block in SST
        writeStl(apu, 0x7000, 0xCFF0, 0);
        startChannel0(apu, 0x7000);
        check(run(apu, 24) == expectRepeat(a, 24), "block at $CFF0 plays");
    }

    std::printf("STL loop word is the sample index to wrap to\n");
    {
        APU apu;
        writeBlock(apu, 0x9000, 0x00, kA);
        writeBlock(apu, 0x9010, 0x04, kB);
        writeStl(apu, 0x7000, 0x9000, 12);   // attack = A once, sustain = B forever
        startChannel0(apu, 0x7000);
        std::vector<int> want = a;
        for (int i = 0; i < 48; i++) want.push_back(b[i % 12]);
        check(run(apu, 12 + 48) == want, "A once, then B repeating");
    }
    {
        APU apu;
        writeBlock(apu, 0x9000, 0x00, kA);
        writeBlock(apu, 0x9010, 0x04, kB);
        writeStl(apu, 0x7000, 0x9000, 18);   // mid-block loop point
        startChannel0(apu, 0x7000);
        std::vector<int> want = ab;
        for (int i = 0; i < 40; i++) want.push_back(b[6 + (i % 6)]);
        check(run(apu, 24 + 40) == want, "loop point can land mid-block");
    }
    {
        APU apu;
        writeBlock(apu, 0x9000, 0x04, kA);
        writeStl(apu, 0x7000, 0x9000, 999);  // past the end: falls back to whole sample
        startChannel0(apu, 0x7000);
        check(run(apu, 24) == expectRepeat(a, 24), "out-of-range loop word loops the whole sample");
    }

    std::printf("Z bit on the final block = one-shot\n");
    {
        APU apu;
        writeBlock(apu, 0x9000, 0x0C, kA);   // Z | W
        writeStl(apu, 0x7000, 0x9000, 0);
        startChannel0(apu, 0x7000);
        std::vector<int> want = a;
        want.resize(36, 0);                  // then silence
        check(run(apu, 36) == want, "plays once then goes silent");

        // Rewriting the same STL address must retrigger it.
        apu.writeByte(0x50, 0x00);
        apu.writeByte(0x51, 0x70);
        check(run(apu, 12) == a, "same STL address retriggers after it finished");
    }

    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "all MMP tests passed",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
