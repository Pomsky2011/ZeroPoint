#!/usr/bin/env python3
"""End-to-end check of the stream port + CPU player. A synthetic stereo WAV (440 Hz left, 1 kHz right) is built into
a ROM, run headlessly in the whole System, and the rendered audio is fitted against the ideal sines.

    python3 -I tools/test_stream_rom.py [build-dir]      (needs numpy, ffmpeg and ZPdevtools next to this repo)

Two cases: an unsigned ROM on the default boot stub, and (if ../ZPbootROM/build/bootrom.bin exists) a v3-signed ROM
on the real boot ROM, where every 16 KB chunk of audio must be verified by the Boot ROM before it is DMAed (an
unverified chunk reads as DC poison). A dropout, click, swapped channel, wrong rate or unverified chunk shows up as
a poor fit. Exits non-zero on failure."""
import os, subprocess, sys, tempfile, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
build = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build")
BOOT = os.path.join(ROOT, "..", "ZPbootROM", "build", "bootrom.bin")
FL, FR = 440.0, 1000.0
WIN = 0.25          # seconds per analysis window
GOOD_DB = 30        # 8-bit quantization of a 0.75 FS sine fits at ~39 dB

def fit_snr_db(x, freq, rate):
    """SNR of x against the best-fit sine at freq (amplitude/phase free)."""
    t = np.arange(len(x)) / rate
    basis = np.stack([np.sin(2 * np.pi * freq * t), np.cos(2 * np.pi * freq * t)], axis=1)
    coef, *_ = np.linalg.lstsq(basis, x, rcond=None)
    resid = x - basis @ coef
    return 10 * np.log10(max(np.sum((basis @ coef) ** 2), 1e-9) / max(np.sum(resid ** 2), 1e-9))

def render(d, src, name, sign, seconds, boot):
    rom, out = os.path.join(d, name + ".zpb"), os.path.join(d, name + ".wav")
    subprocess.run([sys.executable, "-I", os.path.join(HERE, "build_stream_rom.py"), src, rom,
                    "--workdir", os.path.join(d, "work_" + name)] + (["--sign"] if sign else []),
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run([os.path.join(build, "bin", "zpb_wav"), rom, str(seconds), out] + (["--boot", BOOT] if boot else []),
                   check=True, stdout=subprocess.DEVNULL)
    with wave.open(out) as w:
        rate = w.getframerate()
        return rate, np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, 2).astype(float)

def windows(rate, pcm):
    n = int(WIN * rate)
    for i in range(0, len(pcm) - n, n):
        seg = pcm[i:i + n]
        yield i / rate, fit_snr_db(seg[:, 0], FL, rate), fit_snr_db(seg[:, 1], FR, rate), fit_snr_db(seg[:, 0], FR, rate), fit_snr_db(seg[:, 1], FL, rate)

def check(name, rate, pcm, seconds):
    ws = [w for w in windows(rate, pcm)]
    good = [w[1] > GOOD_DB and w[2] > GOOD_DB for w in ws]
    if not any(good):
        print(f"{name}: FAIL - no window has the two tones"); return False
    first = good.index(True); last = len(good) - 1 - good[::-1].index(True)
    span = (last - first + 1) * WIN
    gaps = [ws[i][0] for i in range(first, last + 1) if not good[i]]
    swapped = any(w[3] > 0 or w[4] > 0 for w, g in zip(ws, good) if g)
    ok = not gaps and not swapped and span >= seconds - 2 * WIN
    print(f"{name}: tones from {ws[first][0]:.2f}s for {span:.2f}s (clip {seconds}s), "
          f"{len(gaps)} bad windows inside, channels {'SWAPPED' if swapped else 'ok'} -> {'PASS' if ok else 'FAIL'}")
    return ok

ok = True
with tempfile.TemporaryDirectory(prefix="zpstream_test_") as d:
    results = []
    for name, sign, secs, render_secs in (("unsigned", False, 5, 5), ("signed+boot", True, 8, 24)):
        if sign and not os.path.exists(BOOT):
            print(f"{name}: skipped ({BOOT} not built)"); continue
        src = os.path.join(d, f"sines{secs}.wav")
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", f"sine=frequency={FL:g}:duration={secs}",
                        "-f", "lavfi", "-i", f"sine=frequency={FR:g}:duration={secs}", "-filter_complex",
                        "[0:a][1:a]join=inputs=2:channel_layout=stereo,volume=6[a]", "-map", "[a]", "-ar", "48000", src],
                       check=True)   # sine peaks at 1/8 full scale; x6 = 0.75, so 8-bit quantization is the only noise floor
        rate, pcm = render(d, src, name.split("+")[0], sign, render_secs, sign)
        if not sign:
            pcm = pcm[int(0.25 * rate):]   # skip the CPU's startup and prefill
        ok &= check(name, rate, pcm, secs)
print("PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
