# Exceptions and the Boot ROM's entry table

What the Boot ROM does when a program raises an exception, and how a dev unit gets a handler image from the host.

## What does *not* change

Every exception vector is still read from the **cartridge**: `$00:FFE4` COP, `$00:FFE6` BRK, `$00:FFF8` ABORT, `$00:FFFA` NMI,
`$00:FFFE` IRQ ([interrupts](../../ZPdevtools/docs/cpu/interrupts.md)). The CPU does not route anything to the Boot ROM by itself,
retail or dev, so a program's own handlers work exactly as before. (One exception, dev unit only, in [Faults](#faults).)

What the Boot ROM adds is a table of **entry points** at fixed addresses. A cartridge that wants the Boot ROM's behaviour points
a vector at a one-line stub that jumps there:

```
stub_brk:  jml $E00054
...
.org $FFE6
.word stub_brk
```

`ZPdevtools/examples/cpu/exception_vectors.asm` is a complete cartridge that does this for all five.

| Address | Entry |
|---------|-------|
| `$E0:0044` | marker: `"ZPEX"` in the **dev** Boot ROM, zeros in the retail one |
| `$E0:0048` | ABORT |
| `$E0:004C` | NMI |
| `$E0:0050` | IRQ |
| `$E0:0054` | BRK |
| `$E0:0058` | COP (a signature the Boot ROM does not claim: `$F0-$FF` are its services) |

The hardware has already pushed its usual four-byte frame (PB, PC, P) when an entry runs; both images leave it alone or keep the
stack balanced.

## Retail Boot ROM: it locks

Every entry does what a failed signature check does: the APU is told (mailbox `$A0:1000` = 2, the boot jingle stops), interrupts are
masked, and the CPU halts. Nothing else: no output, no recovery. The retail image contains no code that reads the dev-unit latch or
the serial port.

## Dev Boot ROM: it asks the host for a handler

`bootrom_dev.bin` on a unit whose dev-unit latch (`$D80048` bit 1) is set. With the latch clear it locks like the retail image.
On a dev unit:

1. The registers and the frame are saved into the **context block** at `$81:FF00`.
2. One text line goes out the [debug UART](debug-protocol.md):

   ```
   ZPX? BRK PC=00:8016 P=85 A=1234 X=5678 Y=9ABC SP=01FB D=0000 DB=80 F=00:000000
   ```

   kind (`ABORT NMI IRQ BRK COP`), the interrupted `PB:PC`, `P`, `A X Y SP D DB`, and the fault latch `F=kind:address`
   (below). It is plain ASCII so a person watching the serial console, or a script, can read it.
3. It waits for a **reply**, forever. The request is sent again whenever the line has been idle for about 0.9 s of console time, so
   a host that was not listening yet, or lost the line, just gets asked again:

   | Bytes | |
   |-------|--|
   | `5A 50 58 49` | `"ZPXI"` |
   | length, 2 bytes little endian | 1 to `$FE00` |
   | the image | loaded at `$81:0000` |
   | CRC-16, 2 bytes little endian | CRC-16/CCITT-FALSE of the image (poly `$1021`, init `$FFFF`, MSB first) |

   Bytes that are not a good frame are ignored (a bad length or CRC starts it over and asks again). The reply is read at the UART's
   pace; every byte of it is a host byte, so in a [movie](movies.md) it is recorded like typed input and the whole exception
   replays.
4. Control passes to the image at `$81:0000` with 8-bit A (the kind: 0 ABORT, 1 NMI, 2 IRQ, 3 BRK, 4 COP), 8-bit X/Y, interrupts
   masked and `DB=$81`. The stack still holds the hardware's frame (and the Boot ROM has kept it balanced), so a handler that restores
   the registers can `RTI` and the program carries on. `ZPdevtools/examples/cpu/exception_handler.asm` is exactly that: it prints
   `H<kind><fault kind>` and resumes.

### The context block (`$81:FF00`)

| Offset | |
|--------|--|
| `+00` | kind |
| `+01` | fault kind (below) |
| `+02..04` | fault address, low byte first |
| `+05` | `DB` when the exception was taken |
| `+06` | `P` (from the frame) |
| `+07..08` | `PC`, low byte first |
| `+09` | `PB` |
| `+0A`, `+0C`, `+0E`, `+10`, `+12` | `A`, `X`, `Y`, `SP`, `D`, 16-bit little endian. `SP` points at the frame |
| `+20..+26` | the Boot ROM's own scratch |

`$81:0000-$81:FDFF` is the image, so the handler and its block use the top of the second 64 KiB of Work RAM; the console's Work RAM is
128 KB.

### Giving it the image

The frontends do it for you: `zeropoint_sdl game.zpb --boot ../ZPbootROM/build/bootrom_dev.bin --dev-unit --exception-handler handler.bin`
(`zeropoint_qt` takes the same flag). The file is the raw image; the frontend answers every `ZPX? ` line with it, framed as above. Anything
else on the other end of the UART (a script on `--serial-tcp`, a person pasting bytes) can do the same, and a dev unit without a
responder just keeps asking.

It needs the **real** dev Boot ROM (`--boot`): the native (HLE) boot has no Boot ROM code, so a cartridge's `jml $E0:0054` lands in the
35-byte default stub.

## Faults

Nothing a program does raises an exception today except the instructions that name one (`BRK`, `COP`), a timer or DMA (IRQ) and the
host (NMI, ABORT). **On a dev unit running the dev Boot ROM** the CPU also raises ABORT for a bad access, and routes that ABORT
**straight to `$E0:0048`** instead of through the cartridge's `$00:FFF8` (which a faulting program may well have trashed). The latch
at `$D8004C-$D8004F` says what happened; the dev Boot ROM copies it into the context block and clears it.

| `$D8004C` | |
|-----------|--|
| 0 | none |
| 1 | a **write to ROM** (cartridge, Boot ROM or signed metadata) |
| 2 | a **read** of an unmapped bank |
| 3 | a **write** to an unmapped bank |

`$D8004D-$D8004F` hold the address (low, middle, bank). Writing `$D8004C` clears the latch. The faulting instruction completes
(this is not restartable); the ABORT is taken at the next instruction boundary with the frame pointing past it, so a handler that
`RTI`s carries on after it.

What is **not** a fault: the Boot ROM's own accesses (a handler must not recurse), DMA, the CPU's vector fetches, an instruction fetch
from unmapped memory (it still reads `$FF` and halts), and an empty cartridge slot (it reads its own pattern). A retail unit, and a
dev unit with the retail Boot ROM or the native boot, raise none of this: the latch reads 0 and a store to ROM vanishes as always.
The marker `"ZPEX"` is how the CPU recognises a Boot ROM that handles faults, so none of it can happen with an image that does not.

A fault inside the handler image itself is reported like any other, so a handler that stores to ROM asks for a handler again.

## Determinism

A dev unit's fault latch and the pending ABORT are part of the state hash only with a fault-handling Boot ROM, so a revision 11 dev-unit
movie (which cannot have used one) hashes and replays exactly as before. This is core revision 12.

## Tests

`tools/test_exception.sh` builds the cartridge and the handler from ZPdevtools and runs `bin/test_exception` against both real Boot ROMs:
BRK, COP and the three faults round-trip with A/X/Y intact; no answer means asking again; a bad CRC is ignored; a whole session
replays as a movie; and the retail image, the dev image on a retail unit, and the retail image on a dev unit all lock (about 30 s: the
lock cases wait out the real RSA check).
