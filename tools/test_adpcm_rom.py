#!/usr/bin/env python3
"""End-to-end check of the APU ADPCM player: a synthetic stereo WAV (440 Hz left, 1 kHz right, 258 pages = 1.5 s) is encoded into
a ROM with tools/build_adpcm_rom.py, run headlessly in the whole System (CPU feeder + APU decoder in fast mode + mixer),
and the rendered audio (HLE boot, strict signature check, the player verifies every ADPCM chunk) is fitted against the ideal sines window by window.

    python3 -I tools/test_adpcm_rom.py [build-dir]      (needs numpy, ffmpeg and ZPdevtools next to this repo)

Passes when the tones are present, in the right channels, for the whole 5 s render (3+ trips round the 1.5 s loop:
the only windows allowed to fit badly are the ones that straddle a loop point, where the sine's phase jumps), at
the stream rate the ROM asked for. Exits non-zero on failure."""
import os, subprocess, sys, tempfile, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
build = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build")
FL, FR = 440.0, 1000.0
WIN = 0.25
GOOD_DB = 24        # IMA 4-bit + the stream's 8 bits fit a 0.75 FS sine at ~30 dB or better
LOOP_S, SECONDS = 66052 / 44100, 5

def fit_snr_db(x, freq, rate):
    t = np.arange(len(x)) / rate
    basis = np.stack([np.sin(2 * np.pi * freq * t), np.cos(2 * np.pi * freq * t)], axis=1)
    coef, *_ = np.linalg.lstsq(basis, x, rcond=None)
    resid = x - basis @ coef
    return 10 * np.log10(max(np.sum((basis @ coef) ** 2), 1e-9) / max(np.sum(resid ** 2), 1e-9))

def make_wav(path, rate=44100):
    n = 66052           # 258 whole pages at the ROM's stream rate (44097.7 Hz): the builder stretches nothing, so the tones keep their pitch
    t = np.arange(n) / rate
    pcm = np.stack([0.75 * np.sin(2 * np.pi * FL * t), 0.75 * np.sin(2 * np.pi * FR * t)], axis=1)
    with wave.open(path, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes((pcm * 32767).astype("<i2").tobytes())

def main():
    with tempfile.TemporaryDirectory() as d:
        src, rom, out = (os.path.join(d, n) for n in ("src.wav", "t.zpb", "t.wav"))
        make_wav(src)
        subprocess.run([sys.executable, "-I", os.path.join(HERE, "build_adpcm_rom.py"), src, rom, "--workdir", os.path.join(d, "work")],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run([os.path.join(build, "bin", "zpb_wav"), rom, str(SECONDS), out, "--verify"], check=True, stdout=subprocess.DEVNULL)
        with wave.open(out) as w:
            rate = w.getframerate()
            pcm = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, 2).astype(float)
    n = int(WIN * rate)
    ws = [(i / rate, fit_snr_db(pcm[i:i + n, 0], FL, rate), fit_snr_db(pcm[i:i + n, 1], FR, rate),
           fit_snr_db(pcm[i:i + n, 0], FR, rate), fit_snr_db(pcm[i:i + n, 1], FL, rate)) for i in range(0, len(pcm) - n, n)]
    good = [w[1] > GOOD_DB and w[2] > GOOD_DB for w in ws]
    if not any(good):
        print("FAIL: no window has the two tones"); return 1
    first = good.index(True)
    bad = [ws[i][0] for i in range(first, len(ws)) if not good[i]]
    loops = int(SECONDS / LOOP_S)
    swapped = any(w[3] > 0 or w[4] > 0 for w, g in zip(ws, good) if g)
    ok = first * WIN < 1.0 and len(bad) <= loops and not swapped
    print(f"tones from {ws[first][0]:.2f}s, {len(bad)} bad windows in {SECONDS - ws[first][0]:.1f}s ({loops} loop points allowed), "
          f"channels {'SWAPPED' if swapped else 'ok'}, worst good window {min(w[1] for w, g in zip(ws, good) if g):.1f} dB -> {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1

sys.exit(main())
