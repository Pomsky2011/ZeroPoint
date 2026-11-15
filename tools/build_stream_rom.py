#!/usr/bin/env python3
"""Build a ZeroPoint ROM that streams a WAV through the MMP stream port.

    python3 -I tools/build_stream_rom.py <in.wav> <out.zpb> [--rate HZ] [--seconds N] [--workdir DIR] [--sign]

ffmpeg resamples the WAV to stereo signed 8-bit (triangular dither), the player in
examples/stream-player/player.asm is assembled with cpuasm, and both are packed into an unsigned v1 .zpb (code
at $00:8000, audio from bank 2). Cartridge ROM is 8 MB, which caps song length * rate; the default 13125 Hz
(PITCH $0460, exact) fits about 5 minutes 7 seconds. The converted audio is never written outside --workdir.

By default the ROM is unsigned: run it with the stock boot stub (no --boot). With --sign it goes through zplink and
zpbuild as a v3 ROM (code = banks 0-1, audio = the chunk-verified data region) for the real ZPbootROM (--boot); the
player then asks the Boot ROM to verify each 16 KB chunk before DMAing it.
"""
import argparse, os, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
AUDIO_OFFSET = 0x20000          # bank 2; banks 0-1 hold the player
ROM_MAX = 8 * 1024 * 1024
PAGE = 512                      # one DMA page = 256 frames
MIN_PAGES = 16                  # the player prefills 16 pages

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wav")
    ap.add_argument("out")
    ap.add_argument("--rate", type=int, default=13125, help="stream rate in Hz (default 13125)")
    ap.add_argument("--seconds", type=float, help="only the first N seconds")
    ap.add_argument("--workdir", help="where intermediates go (default: a temp dir)")
    ap.add_argument("--sign", action="store_true", help="v3-sign with zpbuild (dev key) for the real boot ROM")
    ap.add_argument("--title", default="Stream Test")
    args = ap.parse_args()

    pitch = round(args.rate * 4096 / 48000)
    real_rate = pitch * 48000 / 4096
    if abs(real_rate - args.rate) > 0.5:
        print(f"note: {args.rate} Hz is not a PITCH multiple; using {real_rate:.1f} Hz (pitch ${pitch:04X})")

    work = args.workdir or tempfile.mkdtemp(prefix="zpstream_")
    os.makedirs(work, exist_ok=True)

    cmd = ["ffmpeg", "-v", "error", "-y", "-i", args.wav]
    if args.seconds:
        cmd += ["-t", str(args.seconds)]
    cmd += ["-ac", "2", "-af", f"aresample={real_rate:.3f}:dither_method=triangular", "-f", "s8", "-"]
    audio = subprocess.run(cmd, check=True, stdout=subprocess.PIPE).stdout
    audio += bytes(-len(audio) % PAGE)
    audio += bytes(max(0, MIN_PAGES * PAGE - len(audio)))

    if AUDIO_OFFSET + len(audio) > ROM_MAX:
        fit = (ROM_MAX - AUDIO_OFFSET) / (2 * real_rate)
        sys.exit(f"audio is {len(audio) / (2 * real_rate):.1f} s at {real_rate:.0f} Hz but the ROM only holds {fit:.1f} s; "
                 f"use --seconds or a lower --rate")

    end = AUDIO_OFFSET + len(audio)
    with open(os.path.join(work, "stream_cfg.inc"), "w") as f:
        f.write(f".equ PITCH_LO ${pitch & 0xFF:02X}\n.equ PITCH_HI ${pitch >> 8:02X}\n"
                f".equ AUDIO_BANK ${AUDIO_OFFSET >> 16:02X}\n"
                f".equ AUDIO_END_BANK ${end >> 16:02X}\n.equ AUDIO_END_MID ${(end >> 8) & 0xFF:02X}\n"
                f".equ VERIFY_ENABLED ${1 if args.sign else 0:02X}\n")

    tools = os.environ.get("ZPTOOLS", os.path.join(ROOT, "..", "ZPdevtools"))
    shutil.copy(os.path.join(ROOT, "examples", "stream-player", "player.asm"), work)
    shutil.copy(os.path.join(tools, "lib", "zp_dma.asm"), work)
    subprocess.run([os.path.join(tools, "executables", "cpuasm"), "player.asm", "player.bin"], cwd=work, check=True)
    code = open(os.path.join(work, "player.bin"), "rb").read()

    if args.sign:
        with open(os.path.join(work, "audio.bin"), "wb") as f:
            f.write(audio)
        elf = os.path.join(work, "stream.elf")
        subprocess.run([os.path.join(tools, "executables", "zplink"), "-o", elf, "--cpu", "player.bin@0x8000",
                        "--data", f"audio.bin@0x{AUDIO_OFFSET:X}", "--entry", "0x8000"], cwd=work, check=True)
        subprocess.run([os.path.join(tools, "executables", "zpbuild"), elf, "-o", args.out, "--title", args.title,
                        "--dev", "ZP", "--codesize", hex(AUDIO_OFFSET)], check=True)
        print(f"{args.out}: signed v3, {len(audio) / (2 * real_rate):.1f} s at {real_rate:.0f} Hz, pitch ${pitch:04X}")
        return

    rom = bytearray(0x8000) + code
    if len(rom) > AUDIO_OFFSET:
        sys.exit("player does not fit below the audio")
    rom += bytes(AUDIO_OFFSET - len(rom))
    rom += audio
    header = struct.pack("<4sBBHII32s16s", b"ZPB\0", 1, 0, 64, len(rom), 0x8000,
                         args.title.encode()[:31], b"ZP")
    with open(args.out, "wb") as f:
        f.write(header + rom)
    print(f"{args.out}: {len(rom)} bytes, {len(audio) / (2 * real_rate):.1f} s at {real_rate:.0f} Hz, pitch ${pitch:04X}")

if __name__ == "__main__":
    main()
