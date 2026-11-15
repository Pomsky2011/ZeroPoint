#!/usr/bin/env python3
"""Generate the APU program that decodes stereo IMA ADPCM into the MMP stream port, plus its lookup tables.

    gen_adpcm_apu.py <outdir> [--pitch HEX] [--pages N] [--no-flow]

writes outdir/adpcm_apu.asm (apuasm source), outdir/adpcm_apu.bin (assembled, load at $0200),
outdir/adpcm_tables.bin (load at $9000). The CPU side is examples/adpcm-player/player.asm.

How it decodes (the APU has two 8-bit registers and no 16-bit anything, so the work is moved into tables):
  - Nothing is computed per nibble that a table can say. For each magnitude m (0-7): DLO_m/DHI_m = the 16-bit
    difference for every step index, NEXT_m = the next step index, already clamped. Each table is one 256-byte
    page so a single SDP picks it and STRX indexes it with the step index.
  - A frame is one input byte: left nibble high, right low. Jump tables (256 entries, lo and hi page per channel)
    map the whole byte to the address of the stub for that channel's nibble, so there is no nibble extraction:
    X = byte, two STRX loads Y:X = target, JSX. 32 stubs (channel x sign x magnitude), each 19 instructions:
    pred += or -= diff (ADD/ADC or SUB/SBC through memory), idx = NEXT_m[idx].
  - The predictor is never clamped: tools/ima_adpcm.py's encoder keeps it inside int16.
  - Output is the predictor's high byte, written into the stream ring; the page-level work (advance the ring page,
    commit WRPTR, wait for space, refill/loop bookkeeping) runs once per 128 or 256 frames.
Code patches its own SDP operands (input page, output page) at page boundaries: the page is an immediate, and the
data pointer cannot be loaded from memory cheaply.

Memory (APU addresses): vars $0200 (page $02), main loop $0300, page-level routines + start $0400-$05FF, stubs
$0600-$0BFF (0x30 apart, index = channel*16 + nibble), tables $9000-$AFFF (pages $90-$AB), stream ring $3000-$37FF
(8 pages of 128 frames), input ring $3800-$6FFF (56 pages of 256 frames), mailbox: CPU writes the next input page
to fill at $1000, the APU publishes the page it is reading at $1800.
"""
import argparse, os, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ima_adpcm as ima

# --- layout -------------------------------------------------------------------------------------------------
VARS = 0x0200
MAIN = 0x0300
SLOW = 0x0400
START = 0x0500
STUBS = 0x0600
STUB_SIZE = 0x30
TABLE_BASE = 0x9000
DLO, DHI, NEXT = 0x90, 0x98, 0xA0          # table pages, + magnitude
JT_HI = {0: 0xA8, 1: 0xAA}                  # jump-table pages per channel (left, right)
JT_LO = {0: 0xA9, 1: 0xAB}
TABLE_PAGES = 0xAC - 0x90
OUT_PAGE0 = 0x30                            # stream ring: pages $30-$37 (8 pages of 128 frames, power of two)
OUT_PAGES = 8
IN_PAGE0 = 0x38                             # input ring: pages $38-$6F (56 pages of 256 frames = 14 KB). It has to ride out
IN_PAGES = 56                               # the real Boot ROM's chunk verify (~0.52 s): 0.6 s at 24 kHz
assert OUT_PAGES & (OUT_PAGES - 1) == 0 and OUT_PAGE0 + OUT_PAGES == IN_PAGE0 and IN_PAGE0 + IN_PAGES == 0x70
MAILBOX_WR = 0x1000                         # CPU -> APU: input page the CPU fills next
MAILBOX_RD = 0x1800                         # APU -> CPU: input page the APU is reading
PREFILL_PAGES = 4                           # output pages committed before the mixer is enabled

# variable offsets in the var page
PLO = (0x00, 0x03); PHI = (0x01, 0x04); IDX = (0x02, 0x05)
INOFF, OUTOFF, INPG, OUTPG, PREFILL = 0x06, 0x07, 0x08, 0x09, 0x0A
PCNT_LO, PCNT_HI, TOTAL_LO, TOTAL_HI = 0x0C, 0x0D, 0x0E, 0x0F
INIT0 = 0x10                                # six bytes, same order as PLO/PHI/IDX for L then R


def stub_addr(ch, nib):
    return STUBS + (ch * 16 + nib) * STUB_SIZE


class Emit:
    """Collects asm lines and tracks the address so patch points can be named."""
    def __init__(self):
        self.lines = []
        self.pc = 0
        self.tags = {}

    def org(self, addr):
        self.lines.append(f".org ${addr:04X}")
        self.pc = addr

    def rp(self, page):
        self.lines.append(f".rp ${page:02X}")

    def label(self, name):
        self.lines.append(f"{name}:")

    def comment(self, text):
        self.lines.append(f"; {text}")

    def op(self, text, tag=None):
        if tag: self.tags[tag] = self.pc
        self.lines.append(f"    {text}")
        self.pc += 2

    def db(self, values):
        for i in range(0, len(values), 8):          # apuasm drops tokens past the 10th on a line, silently
            chunk = values[i:i + 8]
            self.lines.append("    .db " + ", ".join(f"${v:02X}" for v in chunk))
            self.pc += len(chunk)


def emit_stub(e, ch, nib):
    sub = bool(nib & 8)
    m = nib & 7
    plo, phi, idx = PLO[ch], PHI[ch], IDX[ch]
    lo_op, hi_op = ("sub", "sbc") if sub else ("add", "adc")
    e.org(stub_addr(ch, nib))
    e.comment(f"{'LR'[ch]} nibble {nib:X}: pred {'-' if sub else '+'}= diff[{m}], idx = next[{m}][idx]")
    e.op("sdp $02")
    e.op(f"str ${idx:02X}, x")
    e.op(f"sdp ${DLO + m:02X}")
    e.op("strx x, y")
    e.op("sdp $02")
    e.op(f"str ${plo:02X}, x")
    e.op(f"{lo_op} x, y, ${plo:02X}")
    e.op(f"str ${idx:02X}, x")
    e.op(f"sdp ${DHI + m:02X}")
    e.op("strx x, y")
    e.op("sdp $02")
    e.op(f"str ${phi:02X}, x")
    e.op(f"{hi_op} x, y, ${phi:02X}")
    e.op(f"str ${idx:02X}, x")
    e.op(f"sdp ${NEXT + m:02X}")
    e.op("strx x, y")
    e.op("sdp $02")
    e.op(f"sta y, ${idx:02X}")
    e.op("ret")
    assert e.pc <= stub_addr(ch, nib) + STUB_SIZE


def emit_dispatch(e, ch, tag_in):
    e.op("sdp $02")
    e.op(f"str ${INOFF:02X}, x")
    e.op(f"sdp ${IN_PAGE0:02X}", tag_in)
    e.op("strx x, x")                        # X = the input byte
    e.op(f"sdp ${JT_HI[ch]:02X}")
    e.op("strx x, y")                        # Y = stub page
    e.op(f"sdp ${JT_LO[ch]:02X}")
    e.op("strx x, x")                        # X = stub offset
    e.op("jsx")


def generate(pitch, total_pages, init_l, init_r, flow=True):
    e = Emit()
    # --- variables ---
    e.org(VARS)
    v = [0] * 0x20
    for ch, (p, i) in enumerate((init_l, init_r)):
        p &= 0xFFFF
        v[PLO[ch]], v[PHI[ch]], v[IDX[ch]] = p & 0xFF, p >> 8, i
        v[INIT0 + ch * 3:INIT0 + ch * 3 + 3] = [p & 0xFF, p >> 8, i]
    v[PREFILL] = PREFILL_PAGES
    v[TOTAL_LO], v[TOTAL_HI] = total_pages & 0xFF, total_pages >> 8
    e.comment("variable page ($0200): state, offsets, loop counters, initial state")
    e.db(v)

    # --- main loop: one frame per pass ---
    e.org(MAIN)
    e.rp(MAIN >> 8)
    e.label("main_loop")
    emit_dispatch(e, 0, "in_a")
    emit_dispatch(e, 1, "in_b")
    e.comment("write both predictor high bytes into the stream ring")
    e.op("sdp $02")
    e.op(f"str ${OUTOFF:02X}, x")
    e.op(f"str ${PHI[0]:02X}, y")
    e.op(f"sdp ${OUT_PAGE0:02X}", "out_a")
    e.op("stax y, x")
    e.op("inc x")
    e.op("sdp $02")
    e.op(f"str ${PHI[1]:02X}, y")
    e.op(f"sdp ${OUT_PAGE0:02X}", "out_b")
    e.op("stax y, x")
    e.op("inc x")
    e.op("sdp $02")
    e.op(f"sta x, ${OUTOFF:02X}")
    e.comment("128 frames written: next ring page")
    e.op("zor y")
    e.op("cmg x, y, no_outwrap")
    e.op(f"srp ${SLOW >> 8:02X}")
    e.op("jsr out_page_done")
    e.op(f"srp ${MAIN >> 8:02X}")
    e.label("no_outwrap")
    e.op("sdp $02")
    e.op(f"str ${INOFF:02X}, x")
    e.op("inc x")
    e.op(f"sta x, ${INOFF:02X}")
    e.op("zor y")
    e.op("bne x, y, main_loop")
    e.comment("256 frames read: next input page")
    e.op(f"srp ${SLOW >> 8:02X}")
    e.op("jsr in_page_done")
    e.op(f"srp ${MAIN >> 8:02X}")
    e.op("jms main_loop")
    main_end = e.pc
    assert main_end <= MAIN + 0x100, "main loop outgrew its page"
    t = e.tags

    # --- page-level routines ---
    e.org(SLOW)
    e.rp(SLOW >> 8)
    e.comment("out_page_done: 128 frames of the ring are written. Advance OUTPG, repoint the output SDPs,")
    e.comment("commit WRPTR (high byte first: a low-first commit could leave WRPTR a page behind for one")
    e.comment("instruction and let the mixer replay the ring), enable the mixer after the prefill, wait for room.")
    e.label("out_page_done")
    e.op("sdp $02")
    e.op(f"str ${OUTPG:02X}, x")
    e.op("inc x")
    e.op(f"scr y, ${OUT_PAGES:02X}")
    e.op("cmg y, x, opd_ok")
    e.op("zor x")
    e.label("opd_ok")
    e.op(f"sta x, ${OUTPG:02X}")
    e.op(f"scr y, ${OUT_PAGE0:02X}")
    e.op("add x, y, x")
    e.op(f"sdp ${MAIN >> 8:02X}")
    e.op(f"sta x, ${t['out_a'] + 1 - MAIN:02X}")
    e.op(f"sta x, ${t['out_b'] + 1 - MAIN:02X}")
    e.comment("WRPTR = OUTPG * 128 frames: hi = OUTPG >> 1, lo = (OUTPG & 1) << 7")
    e.op("sdp $02")
    e.op(f"str ${OUTPG:02X}, x")
    e.op("scr y, $01")
    e.op("crb x, y, x")
    e.op("sdp $00")
    e.op("sta x, $79")
    e.op("sdp $02")
    e.op(f"str ${OUTPG:02X}, x")
    e.op("scr y, $01")
    e.op("and x, y, x")
    e.op("scr y, $09")
    e.op("crb x, y, x")
    e.op("sdp $00")
    e.op("sta x, $78")
    if flow:
        e.op("sdp $02")
        e.op(f"str ${PREFILL:02X}, x")
        e.op("zor y")
        e.op("cmg x, y, opd_pf")
        e.op("jms opd_wait")
        e.label("opd_pf")
        e.op("dec x")
        e.op(f"sta x, ${PREFILL:02X}")
        e.op("cmg x, y, opd_wait")
        e.op("scr x, $01")
        e.op("sdp $00")
        e.op("sta x, $70")
        e.label("opd_wait")
        e.comment(f"proceed once the ring has room: (OUTPG - read page) & {OUT_PAGES - 1} < {OUT_PAGES - 2}")
        e.op("sdp $00")
        e.op("str $76, x")
        e.op("scr y, $07")
        e.op("crb x, y, x")
        e.op("str $77, y")
        e.op("add y, y, y")
        e.op("add x, y, x")
        e.op("sdp $02")
        e.op(f"str ${OUTPG:02X}, y")
        e.op("sub y, x, x")
        e.op(f"scr y, ${OUT_PAGES - 1:02X}")
        e.op("and x, y, x")
        e.op(f"scr y, ${OUT_PAGES - 2:02X}")
        e.op("cmg y, x, opd_done")
        e.op("jms opd_wait")
        e.label("opd_done")
    e.op("ret")

    e.comment("in_page_done: 256 frames read. Advance INPG (the input ring), repoint the input SDPs, publish")
    e.comment("the read page, count pages for the whole-track loop (reload the initial state when the track")
    e.comment("ends), wait for the CPU to have filled the next page.")
    e.label("in_page_done")
    e.op("sdp $02")
    e.op(f"str ${INPG:02X}, x")
    e.op("inc x")
    e.op(f"scr y, ${IN_PAGES:02X}")
    e.op("cmg y, x, ipd_ok")
    e.op("zor x")
    e.label("ipd_ok")
    e.op(f"sta x, ${INPG:02X}")
    e.op(f"scr y, ${IN_PAGE0:02X}")
    e.op("add x, y, x")
    e.op(f"sdp ${MAIN >> 8:02X}")
    e.op(f"sta x, ${t['in_a'] + 1 - MAIN:02X}")
    e.op(f"sta x, ${t['in_b'] + 1 - MAIN:02X}")
    e.op("sdp $02")
    e.op(f"str ${INPG:02X}, x")
    e.op(f"sdp ${MAILBOX_RD >> 8:02X}")
    e.op(f"sta x, ${MAILBOX_RD & 0xFF:02X}")
    e.op("sdp $02")
    e.op(f"str ${PCNT_LO:02X}, x")
    e.op("inc x")
    e.op(f"sta x, ${PCNT_LO:02X}")
    e.op("zor y")
    e.op("cmg x, y, ipd_nc")
    e.op(f"str ${PCNT_HI:02X}, x")
    e.op("inc x")
    e.op(f"sta x, ${PCNT_HI:02X}")
    e.label("ipd_nc")
    e.op(f"str ${PCNT_LO:02X}, x")
    e.op(f"str ${TOTAL_LO:02X}, y")
    e.op("cmg x, y, ipd_wait")
    e.op("cmg y, x, ipd_wait")
    e.op(f"str ${PCNT_HI:02X}, x")
    e.op(f"str ${TOTAL_HI:02X}, y")
    e.op("cmg x, y, ipd_wait")
    e.op("cmg y, x, ipd_wait")
    e.comment("end of the track: back to the start (the CPU wraps its ROM pointer in step)")
    e.op("zor x")
    e.op(f"sta x, ${PCNT_LO:02X}")
    e.op(f"sta x, ${PCNT_HI:02X}")
    for i in range(6):
        e.op(f"str ${INIT0 + i:02X}, x")
        e.op(f"sta x, ${i:02X}")
    e.label("ipd_wait")
    if flow:
        e.op(f"sdp ${MAILBOX_WR >> 8:02X}")
        e.op(f"str ${MAILBOX_WR & 0xFF:02X}, x")
        e.op("sdp $02")
        e.op(f"str ${INPG:02X}, y")
        e.op("beq x, y, ipd_wait")
    e.op("ret")
    assert e.pc <= START, "page-level routines outgrew their space"

    # --- start ---
    e.org(START)
    e.rp(START >> 8)
    e.label("start")
    e.op("scr x, $00")
    e.op("scr y, $01")
    e.op("bsp")
    e.comment("silence whatever ran before us: the boot jingle leaves its channels looping (an endless 880 Hz drone)")
    e.op("sdp $00")
    for ch in range(16):
        e.op(f"zoa ${0x50 + 2 * ch:02X}")
    e.comment("stream port: BASE=$3000, SIZE=OUT_PAGES*128 frames, RESET, PITCH, VOL (the mixer is enabled after the prefill)")
    e.op("sdp $00")
    for reg, val in ((0x72, 0x00), (0x73, OUT_PAGE0), (0x74, 0x00), (0x75, (OUT_PAGES * 128) >> 8), (0x70, 0x02),
                     (0x7A, pitch & 0xFF), (0x7B, pitch >> 8), (0x7C, 0xFF)):
        e.op(f"scr x, ${val:02X}")
        e.op(f"sta x, ${reg:02X}")
    if not flow:
        e.op("scr x, $01")
        e.op("sta x, $70")
    e.op(f"srp ${SLOW >> 8:02X}")
    e.op("jsr ipd_wait")
    e.op(f"srp ${MAIN >> 8:02X}")
    e.op("jms main_loop")
    assert e.pc <= STUBS

    for ch in range(2):
        for nib in range(16):
            emit_stub(e, ch, nib)

    return "\n".join(e.lines) + "\n"


def tables_image():
    lo, hi, nxt = ima.apu_tables()
    img = bytearray(TABLE_PAGES * 256)

    def put(page, data):
        img[(page - 0x90) * 256:(page - 0x90) * 256 + len(data)] = data
    for m in range(8):
        put(DLO + m, lo[m]); put(DHI + m, hi[m]); put(NEXT + m, nxt[m])
    for ch in range(2):
        for b in range(256):
            nib = (b >> 4) if ch == 0 else (b & 15)
            a = stub_addr(ch, nib)
            img[(JT_LO[ch] - 0x90) * 256 + b] = a & 0xFF
            img[(JT_HI[ch] - 0x90) * 256 + b] = a >> 8
    return bytes(img)


def assemble(asm_text, workdir, apuasm):
    os.makedirs(workdir, exist_ok=True)
    src, out = os.path.join(workdir, "adpcm_apu.asm"), os.path.join(workdir, "adpcm_apu.bin")
    with open(src, "w") as f:
        f.write(asm_text)
    subprocess.run([apuasm, src, out], check=True, stdout=subprocess.DEVNULL)
    with open(out, "rb") as f:
        return f.read()


def default_apuasm():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(os.environ.get("ZPTOOLS", os.path.join(root, "..", "ZPdevtools")), "executables", "apuasm")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    ap.add_argument("--pitch", default="0EB3", help="stream PITCH, hex (rate = PITCH/4096*48000 Hz)")
    ap.add_argument("--pages", type=int, default=16, help="input pages in the track (loop length)")
    ap.add_argument("--no-flow", action="store_true", help="test build: no waits on the mailbox or the ring")
    args = ap.parse_args()
    text = generate(int(args.pitch, 16), args.pages, (0, 0), (0, 0), flow=not args.no_flow)
    code = assemble(text, args.outdir, default_apuasm())
    with open(os.path.join(args.outdir, "adpcm_tables.bin"), "wb") as f:
        f.write(tables_image())
    print(f"{args.outdir}: {len(code)} bytes of code at ${VARS:04X}, tables at ${TABLE_BASE:04X}")


if __name__ == "__main__":
    main()
