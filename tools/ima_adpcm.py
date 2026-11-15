#!/usr/bin/env python3
"""IMA/DVI 4-bit ADPCM for the ZeroPoint APU decoder (tools/gen_adpcm_apu.py).

The codec is the standard IMA algorithm (89-entry step table, index deltas -1 -1 -1 -1 2 4 6 8) with one
constraint: the encoder never lets the predictor leave the int16 range. The APU decoder has no room for the
clamp, so a stream from here decodes identically on a standard IMA decoder (which would clamp) and on the APU
(which does not), and decode() below is the exact reference for both.

Stream layout (stereo, what the APU player consumes): one byte per frame, left nibble in the high half, right in
the low half. Both channels start from INIT_STATE (predictor = first sample, step index 0), and a looping player
reloads it at the loop point, so no per-block headers are needed.
"""
import subprocess

STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767,
]
INDEX_DELTA = [-1, -1, -1, -1, 2, 4, 6, 8]   # by the magnitude bits of the nibble
N_STEPS = len(STEP_TABLE)                      # 89
PAGE_FRAMES = 256                              # one input page = 256 frames = 256 bytes


def diff_for(step, m):
    """The standard IMA difference for magnitude bits m (0-7) at a step size, with its per-shift truncation."""
    d = step >> 3
    if m & 1: d += step >> 2
    if m & 2: d += step >> 1
    if m & 4: d += step
    return d


def next_index(idx, m):
    return min(N_STEPS - 1, max(0, idx + INDEX_DELTA[m]))


def decode_nibble(pred, idx, nib):
    d = diff_for(STEP_TABLE[idx], nib & 7)
    return (pred - d if nib & 8 else pred + d), next_index(idx, nib & 7)


def _encode_nibble(pred, idx, target):
    step = STEP_TABLE[idx]
    diff = target - pred
    nib = 8 if diff < 0 else 0
    ad = -diff if diff < 0 else diff
    if ad >= step: nib |= 4; ad -= step
    step >>= 1
    if ad >= step: nib |= 2; ad -= step
    step >>= 1
    if ad >= step: nib |= 1
    new, nidx = decode_nibble(pred, idx, nib)
    if -32768 <= new <= 32767:
        return nib, new, nidx
    # the predictor would leave int16 (a standard decoder would clamp, the APU would wrap): take the in-range
    # nibble that lands closest to the target
    best = None
    for n in range(16):
        p2, i2 = decode_nibble(pred, idx, n)
        if -32768 <= p2 <= 32767 and (best is None or abs(target - p2) < best[0]):
            best = (abs(target - p2), n, p2, i2)
    return best[1], best[2], best[3]


def encode_channel(samples, init):
    """samples: iterable of int16; init = (predictor, index). Returns a list of nibbles."""
    pred, idx = init
    out = []
    for s in samples:
        nib, pred, idx = _encode_nibble(pred, idx, s)
        out.append(nib)
    return out


def decode_channel(nibbles, init):
    pred, idx = init
    out = []
    for n in nibbles:
        pred, idx = decode_nibble(pred, idx, n)
        out.append(pred)
    return out


def init_state(first_l, first_r):
    return (first_l, 0), (first_r, 0)


def encode_stereo(left, right):
    """Returns (stream bytes, init_l, init_r). Frames are padded to a multiple of PAGE_FRAMES by the caller."""
    assert len(left) == len(right)
    il, ir = init_state(left[0], right[0])
    nl, nr = encode_channel(left, il), encode_channel(right, ir)
    return bytes((a << 4) | b for a, b in zip(nl, nr)), il, ir


def decode_stereo(stream, il, ir):
    left = decode_channel([b >> 4 for b in stream], il)
    right = decode_channel([b & 15 for b in stream], ir)
    return left, right


def apu_tables():
    """What the APU decoder indexes by step index: for each magnitude m, the difference's low byte, its high byte
    and the next (already clamped) index. Each is 89 bytes; the APU gives each its own 256-byte page."""
    lo, hi, nxt = [], [], []
    for m in range(8):
        d = [diff_for(s, m) for s in STEP_TABLE]
        assert max(d) < 0xF000                      # high byte stays clear of $FF (the APU's SBC adds the borrow to it)
        lo.append(bytes(x & 0xFF for x in d))
        hi.append(bytes(x >> 8 for x in d))
        nxt.append(bytes(next_index(i, m) for i in range(N_STEPS)))
    return lo, hi, nxt


def read_pcm(path, rate, frames=None, seconds=None):
    """Decode any audio file with ffmpeg to stereo int16 at `rate` Hz (triangular dither off: this feeds an ADPCM
    encoder, not an 8-bit quantizer). If `frames` is given the stream is time-stretched by a hair (the decoder
    resamples) so it is exactly that long: a seamless whole-track loop needs a whole number of pages."""
    af = []
    if frames is not None:
        nat = _probe_frames(path, rate, seconds)
        af.append(f"asetrate={_probe_rate(path) * nat / frames:.6f}")   # play the source `frames/nat` times as long
    af.append(f"aresample={rate}")
    cmd = ["ffmpeg", "-v", "error", "-y", "-i", path]
    if seconds: cmd += ["-t", str(seconds)]
    cmd += ["-ac", "2", "-af", ",".join(af), "-f", "s16le", "-"]
    raw = subprocess.run(cmd, check=True, stdout=subprocess.PIPE).stdout
    import array, sys
    a = array.array("h")
    a.frombytes(raw[:len(raw) // 4 * 4])
    if sys.byteorder == "big": a.byteswap()
    left, right = list(a[0::2]), list(a[1::2])
    if frames is not None:
        left, right = left[:frames], right[:frames]
        left += [left[-1]] * (frames - len(left)); right += [right[-1]] * (frames - len(right))
    return left, right


def _probe_rate(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "stream=sample_rate",
                          "-of", "default=nw=1:nk=1", path], check=True, stdout=subprocess.PIPE, text=True).stdout.strip()
    return float(out)


def _probe_frames(path, rate, seconds):
    out = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "default=nw=1:nk=1", path],
                         check=True, stdout=subprocess.PIPE, text=True).stdout.strip()
    dur = float(out)
    if seconds: dur = min(dur, seconds)
    return round(dur * rate)
