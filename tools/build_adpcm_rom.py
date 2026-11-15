#!/usr/bin/env python3
"""Build a ZeroPoint ROM that plays a looping track through the APU's ADPCM decoder.

    python3 -I tools/build_adpcm_rom.py <audio> <out.zpb> [--rate HZ] [--seconds N] [--workdir DIR] [--title T]
                                        [--decoded-wav out.wav] [--unsigned]

ffmpeg decodes <audio> (anything it reads) to stereo at the stream rate, the track is stretched by a hair to a whole
number of 256-frame pages (so the whole-track loop has no gap and no overlap), tools/ima_adpcm.py encodes it, and
tools/gen_adpcm_apu.py makes the APU decoder. examples/adpcm-player/player.asm is the CPU feeder. ROM layout: player
at $00:8000, the APU program at $01:0000, the decoder tables at $01:1000, the ADPCM data from bank 2 (banks 0-1 are the
signed code region, the ADPCM is the chunk-verified data region). The ROM is signed by default (zplink + zpbuild, v3
trailer, ZPdevtools dev key), which is what the default frontends' HLE boot requires; the player asks the Boot ROM to
verify each 16 KB chunk of ADPCM the first time round (COP #$FF). --unsigned writes a plain v1 .zpb for
`zeropoint_sdl --no-verify` and the test harnesses. The converted audio never leaves --workdir.

--rate defaults to 44100; the APU decoder needs ~3.4 MIPS for 44.1 kHz stereo, which is most of fast mode.
"""
import argparse, os, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import ima_adpcm as ima
import gen_adpcm_apu as gen

CODE_OFFSET, TABLES_OFFSET, DATA_OFFSET = 0x10000, 0x11000, 0x20000
ROM_MAX = 8 * 1024 * 1024


def pad(data, n=256):
    return data + bytes(-len(data) % n)


def write_wav(path, left, right, rate):
    import wave, array
    a = array.array("h")
    for l, r in zip(left, right):
        a.append(l); a.append(r)
    with wave.open(path, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes(a.tobytes())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("audio")
    ap.add_argument("out")
    ap.add_argument("--rate", type=int, default=44100)
    ap.add_argument("--seconds", type=float, help="only the first N seconds")
    ap.add_argument("--workdir")
    ap.add_argument("--title", default="ADPCM Loop")
    ap.add_argument("--decoded-wav", help="also write what the APU will play (the reference decode) as a WAV")
    ap.add_argument("--unsigned", action="store_true", help="plain v1 .zpb (no signature)")
    args = ap.parse_args()

    pitch = round(args.rate * 4096 / 48000)
    real_rate = pitch * 48000 / 4096
    if abs(real_rate - args.rate) > 0.5:
        print(f"note: {args.rate} Hz is not a PITCH multiple; using {real_rate:.1f} Hz (pitch ${pitch:04X})")
    work = args.workdir or tempfile.mkdtemp(prefix="zpadpcm_")
    os.makedirs(work, exist_ok=True)

    nat = ima._probe_frames(args.audio, real_rate, args.seconds)
    frames = max(1, round(nat / ima.PAGE_FRAMES)) * ima.PAGE_FRAMES
    if DATA_OFFSET + frames > ROM_MAX:
        sys.exit(f"{frames} frames ({frames / real_rate:.1f} s at {real_rate:.0f} Hz) do not fit the 8 MB ROM; use --seconds or a lower --rate")
    print(f"decoding {args.audio}: {nat} frames at {real_rate:.1f} Hz, stretched to {frames} ({frames // ima.PAGE_FRAMES} pages, {frames / real_rate:.2f} s)")
    left, right = ima.read_pcm(args.audio, real_rate, frames, args.seconds)
    print("encoding...")
    stream, il, ir = ima.encode_stereo(left, right)
    pages = frames // ima.PAGE_FRAMES
    if args.decoded_wav:
        dl, dr = ima.decode_stereo(stream, il, ir)
        write_wav(args.decoded_wav, [x >> 8 << 8 for x in dl], [x >> 8 << 8 for x in dr], round(real_rate))   # what the 8-bit ring keeps

    code = gen.assemble(gen.generate(pitch, pages, il, ir), work, gen.default_apuasm())
    tables = gen.tables_image()
    code, tables = pad(code), pad(tables)

    with open(os.path.join(work, "adpcm_cfg.inc"), "w") as f:
        f.write(f".equ CODE_SRC_MID ${(CODE_OFFSET >> 8) & 0xFF:02X}\n.equ CODE_SRC_BANK ${CODE_OFFSET >> 16:02X}\n"
                f".equ CODE_LEN_HI ${len(code) >> 8:02X}\n"
                f".equ TABLES_SRC_MID ${(TABLES_OFFSET >> 8) & 0xFF:02X}\n.equ TABLES_SRC_BANK ${TABLES_OFFSET >> 16:02X}\n"
                f".equ TABLES_LEN_HI ${len(tables) >> 8:02X}\n"
                f".equ DATA_BANK ${DATA_OFFSET >> 16:02X}\n.equ VERIFY_ENABLED ${0 if args.unsigned else 1:02X}\n"
                f".equ IN_PAGE0 ${gen.IN_PAGE0:02X}\n.equ IN_PAGES ${gen.IN_PAGES:02X}\n"
                f".equ TOTAL_PAGES_LO ${pages & 0xFF:02X}\n.equ TOTAL_PAGES_HI ${pages >> 8:02X}\n")
    assert len(code) <= TABLES_OFFSET - CODE_OFFSET and len(tables) <= DATA_OFFSET - TABLES_OFFSET

    tools = os.environ.get("ZPTOOLS", os.path.join(ROOT, "..", "ZPdevtools"))
    shutil.copy(os.path.join(ROOT, "examples", "adpcm-player", "player.asm"), work)
    shutil.copy(os.path.join(tools, "lib", "zp_dma.asm"), work)
    subprocess.run([os.path.join(tools, "executables", "cpuasm"), "player.asm", "player.bin"], cwd=work, check=True,
                   stdout=subprocess.DEVNULL)
    player = open(os.path.join(work, "player.bin"), "rb").read()

    if not args.unsigned:
        if len(player) + 0x8000 > CODE_OFFSET:
            sys.exit("player does not fit below the APU program")
        for name, blob in (("player.bin", player), ("apu.bin", code), ("tables.bin", tables), ("stream.bin", stream)):
            with open(os.path.join(work, name), "wb") as f:
                f.write(blob)
        elf = os.path.join(work, "adpcm.elf")
        subprocess.run([os.path.join(tools, "executables", "zplink"), "-o", elf, "--cpu", "player.bin@0x8000",
                        "--cpu", f"apu.bin@0x{CODE_OFFSET:X}", "--cpu", f"tables.bin@0x{TABLES_OFFSET:X}",
                        "--data", f"stream.bin@0x{DATA_OFFSET:X}", "--entry", "0x8000"], cwd=work, check=True,
                       stdout=subprocess.DEVNULL)
        subprocess.run([os.path.join(tools, "executables", "zpbuild"), elf, "-o", args.out, "--title", args.title,
                        "--dev", "ZP", "--codesize", hex(DATA_OFFSET)], check=True, stdout=subprocess.DEVNULL)
        print(f"{args.out}: signed v3, {frames / real_rate:.1f} s at {real_rate:.0f} Hz stereo, pitch ${pitch:04X}, loops every {pages} pages")
        return

    rom = bytearray(0x8000) + player
    if len(rom) > CODE_OFFSET:
        sys.exit("player does not fit below the APU program")
    rom += bytes(CODE_OFFSET - len(rom)) + code
    rom += bytes(TABLES_OFFSET - len(rom)) + tables
    rom += bytes(DATA_OFFSET - len(rom)) + stream
    header = struct.pack("<4sBBHII32s16s", b"ZPB\0", 1, 0, 64, len(rom), 0x8000, args.title.encode()[:31], b"ZP")
    with open(args.out, "wb") as f:
        f.write(header + rom)
    print(f"{args.out}: {len(rom)} bytes, {frames / real_rate:.1f} s at {real_rate:.0f} Hz stereo, pitch ${pitch:04X}, loops every {pages} pages")


if __name__ == "__main__":
    main()
