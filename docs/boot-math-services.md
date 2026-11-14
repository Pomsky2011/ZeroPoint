# Boot ROM math services (COP #$F0-#$F6)

Trig and square-root calls into the real ZPbootROM (`ZPbootROM/def88186/trig.def`), GBA-SWI
style: one `COP` signature per function. They need the real Boot ROM (`--boot`,
`System::loadBootROM()`); the 35-byte default stub has no slot table, so with it
`COP #$F0`-`#$FE` stay the cartridge's own software interrupt via `$00:FFE4`.

| COP | name   | in                          | out                                              |
|-----|--------|-----------------------------|--------------------------------------------------|
| $F0 | SIN    | A = angle                   | A = sin, Q1.15                                   |
| $F1 | COS    | A = angle                   | A = cos, Q1.15                                   |
| $F2 | SINCOS | A = angle                   | A = sin, X = cos                                 |
| $F3 | ATAN2  | X = x, Y = y (signed)       | A = angle (atan2(0,0) = 0)                       |
| $F4 | ATAN   | A = ratio, signed Q8.8      | A = angle in [-$4000, +$4000] (signed word)      |
| $F5 | SQRT   | A = low word, X = high word | A = floor(sqrt(X:A)), 0..65535                   |
| $F6 | HYPOT  | X = x, Y = y (signed)       | A = floor(sqrt(x*x+y*y)), 0..46340               |
| $F7-$FE | reserved | -                       | return immediately, registers untouched          |

`COP #$FF` is still `verify_chunk` (`zpb-format.md`).

**Formats.** Angle: unsigned, 65536 = one turn, 0 = +x, $4000 = +y (counter-clockwise).
Q1.15: $7FFF ~ +1.0; the table is symmetric so -1.0 is $8001, never $8000.

**Accuracy** (vs `math.*`): sin/cos within ~1 LSB of Q1.15; atan2 within ~1 angle unit
(0.0055 degrees). SQRT/HYPOT are exact floors.

**Contract.** The caller's M/X width doesn't matter (every service does `REP #$30`; `RTI`
restores P, so flags come back unchanged). DB, D and SP are preserved. Other registers are
clobbered. IRQs are masked for the call. Clobbers WRAM `$80:2A00-$80:2A3F`. Unlike
`COP #$FF` these don't set `bootServiceActive` - there's no privilege in them.

**Cost** (worst case measured, CPU cycles incl. COP/RTI): SIN/COS ~175, SINCOS ~335,
ATAN2/ATAN ~1090, SQRT ~3900, HYPOT ~4000.

**Layout.** `COP #n` jumps to the 4-byte `jml` at `$E0:0008 + 4*(n-$F0)` (right after the reset
and `verify_chunk` vectors). The tables are patched into bank $E0 at `$F000` (SIN_TAB, 258
words: round(32767 sin(i pi/512)), 257th entry repeated) and `$F204` (ATAN_TAB, 258 words:
atan(i/256) as an angle) by `build.sh`; the code reads them with literal 24-bit addresses
because `lda label,X` is DB-relative and DB is the caller's.

`ZPbootROM/def88186/trig_model.py` is the bit-exact reference; `bin/test_boot_trig <bootrom.bin>
<trig_model.py>` runs all of its vectors through real `COP`s.
