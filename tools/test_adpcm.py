#!/usr/bin/env python3 -I
"""Tests for the APU ADPCM decoder: python3 -I tools/test_adpcm.py [--keep DIR]

1. the codec: encoder output decodes identically with and without a clamp (no overflow), and tracks the input
2. the APU: tools/gen_adpcm_apu.py's program, run in build/bin/test_adpcm_apu with a CPU-feeder and a mixer
   stand-in, writes exactly the reference decode's high bytes into the stream ring, over several loops of the track
   (a short track, and one longer than the 56-page input ring), in normal and fast mode, and reports the cost.
"""
import math, os, random, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import ima_adpcm as ima
import gen_adpcm_apu as gen

HARNESS = os.environ.get("TEST_ADPCM_APU", os.path.join(ROOT, "build", "bin", "test_adpcm_apu"))
failures = 0


def check(ok, what):
    global failures
    print(f"  {'PASS' if ok else 'FAIL'} {what}")
    if not ok: failures += 1


def signal(frames, seed):
    """Loud stereo test material: two tones, noise, a few full-scale clipped bursts (the encoder's overflow case)."""
    rnd = random.Random(seed)
    l, r = [], []
    for i in range(frames):
        t = i / 44100.0
        a = 0.55 * math.sin(2 * math.pi * 440 * t) + 0.3 * math.sin(2 * math.pi * 3150 * t) + rnd.uniform(-0.08, 0.08)
        b = 0.5 * math.sin(2 * math.pi * 523 * t + 1) + 0.35 * math.sin(2 * math.pi * 5000 * t) + rnd.uniform(-0.08, 0.08)
        if (i // 700) % 5 == 4:                       # hard-clipped bursts
            a, b = max(-1, min(1, a * 4)), max(-1, min(1, b * 4))
        l.append(int(max(-1, min(1, a)) * 32767)); r.append(int(max(-1, min(1, b)) * 32767))
    return l, r


def test_codec():
    print("codec")
    l, r = signal(ima.PAGE_FRAMES * 20, 1)
    stream, il, ir = ima.encode_stereo(l, r)
    dl, dr = ima.decode_stereo(stream, il, ir)
    check(len(stream) == len(l), "one byte per frame")
    check(all(-32768 <= x <= 32767 for x in dl + dr), "the unclamped decode stays inside int16")
    err = [abs(a - b) for a, b in zip(l, dl)]
    check(sum(err) / len(err) < 900, f"decode tracks the input (mean error {sum(err) / len(err):.0f} of 32767)")
    # tables agree with the shift-and-add definition
    lo, hi, nxt = ima.apu_tables()
    check(all(lo[m][i] | (hi[m][i] << 8) == ima.diff_for(ima.STEP_TABLE[i], m) for m in range(8) for i in range(89)), "tables hold the IMA differences")


def run_apu(work, pages, mode_fast, loops, label):
    l, r = signal(ima.PAGE_FRAMES * pages, pages)
    stream, il, ir = ima.encode_stereo(l, r)
    text = gen.generate(0x0EB3, pages, il, ir, flow=True)
    code = gen.assemble(text, work, gen.default_apuasm())
    with open(os.path.join(work, "tables.bin"), "wb") as f: f.write(gen.tables_image())
    with open(os.path.join(work, "input.bin"), "wb") as f: f.write(stream)
    frames = ima.PAGE_FRAMES * pages * loops
    out = os.path.join(work, "out.bin")
    cmd = [HARNESS, os.path.join(work, "adpcm_apu.bin"), os.path.join(work, "tables.bin"), os.path.join(work, "input.bin"),
           f"{gen.START:04X}", out, "--bytes", str(frames * 2), "--max-seconds", "60",
           "--in-page0", f"{gen.IN_PAGE0:X}", "--in-pages", str(gen.IN_PAGES), "--out-page0", f"{gen.OUT_PAGE0:X}", "--out-pages", str(gen.OUT_PAGES)] + (["--fast"] if mode_fast else [])
    res = subprocess.run(cmd, stdout=subprocess.PIPE, text=True)
    print("   ", res.stdout.strip().replace("\n", "\n    "))
    got = open(out, "rb").read()
    dl, dr = ima.decode_stereo(stream, il, ir)
    want = bytearray()
    for _ in range(loops):
        for a, b in zip(dl, dr):
            want += bytes([(a >> 8) & 0xFF, (b >> 8) & 0xFF])
    check(len(got) == len(want), f"{label}: {frames} frames recorded")
    bad = next((i for i in range(min(len(got), len(want))) if got[i] != want[i]), None)
    check(bad is None, f"{label}: every ring byte matches the reference" + ("" if bad is None else f" (first difference at byte {bad}: got {got[bad]:02X}, want {want[bad]:02X})"))
    return res.stdout


def main():
    keep = None
    if "--keep" in sys.argv: keep = sys.argv[sys.argv.index("--keep") + 1]
    if not os.path.exists(HARNESS):
        sys.exit(f"build {HARNESS} first")
    test_codec()
    with tempfile.TemporaryDirectory() as tmp:
        work = keep or tmp
        os.makedirs(work, exist_ok=True)
        print("APU decoder, fast mode, 6-page track looped 5 times (state reload, mixer + ring flow control)")
        run_apu(work, 6, True, 5, "6 pages x5")
        print("APU decoder, fast mode, 60-page track (longer than the 56-page input ring) looped twice")
        run_apu(work, 60, True, 2, "60 pages x2")
        print("APU decoder, normal mode")
        run_apu(work, 6, False, 3, "normal mode")
    print("PASS" if not failures else f"{failures} FAILED")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
