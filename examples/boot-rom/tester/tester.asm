; tester.asm - ZeroPoint hardware self-test, packaged as an alternate Boot
; ROM payload. Runs with no cartridge (`zeropoint_sdl --boot tester.bin`) and
; exercises CPU ALU/branches, Work RAM, the PPU register bus, APU control/
; status, the Timer registers, the Interrupt controller, and the DMA
; controller directly from bare boot-ROM code (no C runtime, no cartridge).
;
; Each test writes a single result byte to a fixed Work RAM address:
;   $01 = pass, $00 = fail, $AA = never reached (crash/hang before this
;   test ran - the whole block is sentinel-initialized to $AA up front so a
;   wedged CPU is visibly distinguishable from a real fail).
;
;   $800030  T0  ALU add (ADC)
;   $800031  T1  ALU subtract (SBC)
;   $800032  T2  Bitwise AND/ORA/XOR
;   $800033  T3  Work RAM read/write round-trip
;   $800034  T4  PPU register bus round-trip ($D80008-$D8000A)
;   $800035  T5  APU control -> status round-trip ($D80010/$D80011)
;   $800036  T6  Timer control register round-trip ($D80050)
;   $800037  T7  Interrupt controller enable-mask round-trip ($D80059)
;   $800038  T8  DMA controller: RAM-to-RAM copy via channel 0
;   $800039  Aggregate: $01 iff T0-T8 all passed, else $00
;
; A C++ harness (tools/test_diag_rom.cpp) drives this headlessly and reads
; the block back. When run visually under zeropoint_sdl, the word "PASS" is
; also drawn as a 15x5-pixel bitmap font (position computed from the live
; beam, not fixed - see the "Visual indicator" section below) iff every
; test passed - CPU-only, via the pixel-draw port ($B00100-$B0010A), so no
; PPU microcode needs to run (TILEDRAW is a PPU instruction and can't be
; issued from boot-ROM CPU code, since the PPU's own program counter is
; never started here) and the indicator stays a plain grayscale white/
; nothing signal (see CLAUDE.md's default-palette note - only white is
; available without loading a real palette). The glyph coordinates were
; generated from a 3x5 bitmap font (P/A/S, 1px spacing between letters) by
; a scratch script rather than authored by hand, since hand-picked pixel
; coordinates for text are exactly the kind of thing that silently produces
; "weird patterns instead of proper text" when a single coordinate is off.
;
; Scope: T4/T5/T6/T7 are register/bus round-trips - they confirm the CPU
; can store a byte into the peripheral's control register and read a
; consistent value back, which catches address-decode/register-storage
; bugs but NOT a peripheral whose control logic is silently dead (e.g. a
; timer register that accepts writes but never actually schedules an
; event). T0-T3 are plain CPU/RAM correctness. T8 is the one test that
; verifies real subsystem *behavior*, not just register plumbing: it reads
; back bytes that only land at the destination if the DMA controller
; actually executed a multi-cycle transfer (confirmed by removing the
; trigger byte in a scratch build and observing T8 correctly go to FAIL).
;
; Assembler notes (see ZPdevtools memory: cpuasm-branch-constraints,
; apu-toolchain-encoding-bugs, and examples/boot-rom/demo-cpu-ppu-apu's
; README for the empirical findings this file relies on):
;   - .BANK $E0 makes BEQ/BCS/BGE/BRA (all label-based, AM_ABSOLUTE_LONG)
;     resolve within this bank instead of silently defaulting to bank $00.
;   - No BNE/BPL/CPX - "not equal" is synthesized as BEQ-past/BRA-fail, and
;     nothing here needs an indexed counter.
;   - No BCC (aliases to BCS in the interpreter) - never used below.
;   - AM_ABSOLUTE_LONG only exists for LDA/STA, not ADC/SBC/AND/ORA/CMP -
;     every ALU op below reads its memory operand into A via `LDA long`
;     first, then operates with an immediate, matching zp_dma.asm's
;     pattern. Aggregation of the 9 result bytes is done as a chain of
;     `LDA long / CMP #$01 / BEQ` rather than ANDing memory directly, for
;     the same reason.
;   - Every memory operand is a literal 24-bit address ($8000xx / $D800xx /
;     $B00xxx), which cpuasm auto-promotes to the bank-safe long form - so
;     this file never needs SDB (no same-bank JSR/JMP/absolute label reads
;     are used at all; everything is either a literal or a BEQ/BRA branch).

.BANK $E0

start:
        ; --- sentinel-init the result block ---
        LDA #$AA
        STA $800030
        STA $800031
        STA $800032
        STA $800033
        STA $800034
        STA $800035
        STA $800036
        STA $800037
        STA $800038
        STA $800039

; ============================================================
; T0: ALU add (ADC) - 0x05 + 0x03 should be 0x08
; ============================================================
t0:
        LDA #$05
        CLC
        ADC #$03
        CMP #$08
        BEQ t0_pass
        LDA #$00
        BRA t0_store
t0_pass:
        LDA #$01
t0_store:
        STA $800030

; ============================================================
; T1: ALU subtract (SBC) - 0x0A - 0x04 should be 0x06
; ============================================================
t1:
        LDA #$0A
        SEC
        SBC #$04
        CMP #$06
        BEQ t1_pass
        LDA #$00
        BRA t1_store
t1_pass:
        LDA #$01
t1_store:
        STA $800031

; ============================================================
; T2: bitwise AND / ORA / XOR
;   0xF0 AND 0x3C = 0x30
;   0x0F ORA 0x30 = 0x3F
;   0xFF XOR 0xAA = 0x55
; ============================================================
t2:
        LDA #$F0
        AND #$3C
        CMP #$30
        BEQ t2_b
        BRA t2_fail
t2_b:
        LDA #$0F
        ORA #$30
        CMP #$3F
        BEQ t2_c
        BRA t2_fail
t2_c:
        LDA #$FF
        XOR #$AA
        CMP #$55
        BEQ t2_pass
t2_fail:
        LDA #$00
        BRA t2_store
t2_pass:
        LDA #$01
t2_store:
        STA $800032

; ============================================================
; T3: Work RAM read/write round-trip at an address distinct
; from the result/scratch blocks
; ============================================================
t3:
        LDA #$5A
        STA $800040
        LDA $800040
        CMP #$5A
        BEQ t3_pass
        LDA #$00
        BRA t3_store
t3_pass:
        LDA #$01
t3_store:
        STA $800033

; ============================================================
; T4: PPU register bus round-trip. Selects PPU register R5 (a
; general-purpose register - R59-R63 are the special ones, see
; CLAUDE.md) via PPUREG_ADDR, writes a 16-bit value through
; PPUREG_DATA, then reads it back through the same port. The
; PPU program counter never runs, so this only proves the CPU
; <-> PPU register bus itself is wired correctly.
; ============================================================
t4:
        LDA #$05
        STA $D80008          ; PPUREG_ADDR = R5
        LDA #$34
        STA $D80009          ; PPUREG_DATA low
        LDA #$12
        STA $D8000A          ; PPUREG_DATA high
        LDA $D80009
        CMP #$34
        BEQ t4_b
        BRA t4_fail
t4_b:
        LDA $D8000A
        CMP #$12
        BEQ t4_pass
t4_fail:
        LDA #$00
        BRA t4_store
t4_pass:
        LDA #$01
t4_store:
        STA $800034

; ============================================================
; T5: APU control -> status round-trip. Issuing APUCTRL halt
; (bit1) should be observable as APUSTATUS bit0 (halted).
; ============================================================
t5:
        LDA #$02              ; APUCTRL: halt
        STA $D80010
        LDA $D80011            ; APUSTATUS
        AND #$01                ; bit0 = halted
        CMP #$01
        BEQ t5_pass
        LDA #$00
        BRA t5_store
t5_pass:
        LDA #$01
t5_store:
        STA $800035

; ============================================================
; T6: Timer control register round-trip. Enable the V-blank
; timer bit, confirm it reads back, then disable it again so it
; doesn't fire mid-test-run.
; ============================================================
t6:
        LDA #$80               ; V (VBlank) bit
        STA $D80050
        LDA $D80050
        CMP #$80
        BEQ t6_pass
        LDA #$00
        BRA t6_disable
t6_pass:
        LDA #$01
t6_disable:
        STA $800036
        LDA #$00
        STA $D80050             ; disable again

; ============================================================
; T7: Interrupt controller top-level mask round-trip. Restore
; the transparent default ($FF) afterward.
; ============================================================
t7:
        LDA #$0F                 ; all 4 real sources (V/H/T/D)
        STA $D80059
        LDA $D80059
        CMP #$0F
        BEQ t7_pass
        LDA #$00
        BRA t7_restore
t7_pass:
        LDA #$01
t7_restore:
        STA $800037
        LDA #$FF
        STA $D80059                ; restore transparent default

; ============================================================
; T8: DMA controller - copy 4 known bytes from $800050 to
; $800060 on channel 0 (DataCopy mode). A single-unit transfer
; always moves a full 256-byte block (see include/dma.h -
; getTotalSize()), so only the first 4 destination bytes are
; checked; the rest of the block is uninitialized source RAM
; and deliberately not inspected.
; ============================================================
t8:
        LDA #$11
        STA $800050
        LDA #$22
        STA $800051
        LDA #$33
        STA $800052
        LDA #$44
        STA $800053

t8_wait0:
        LDA $D80023               ; channel 0 status
        BEQ t8_cfg                 ; 0 = Idle
        CMP #$04
        BEQ t8_cfg                  ; 4 = Complete
        BRA t8_wait0

t8_cfg:
        LDA #$00
        STA $D80020                  ; reset config write index
        LDA #$00
        STA $D80021                   ; config byte: size=00 mode=00(Copy) chan=0
        LDA #$50
        STA $D80021                    ; src low
        LDA #$00
        STA $D80021                     ; src mid
        LDA #$80
        STA $D80021                      ; src bank
        LDA #$60
        STA $D80021                       ; dst low
        LDA #$00
        STA $D80021                        ; dst mid
        LDA #$80
        STA $D80021                         ; dst bank
        LDA #$01
        STA $D80021                          ; size multiplier = 1 (256 bytes)
        LDA #$00
        STA $D80021                           ; trigger byte - fires the transfer

t8_wait1:
        LDA $D80023
        BEQ t8_wait1_done
        CMP #$04
        BEQ t8_wait1_done
        BRA t8_wait1
t8_wait1_done:

        LDA $800060
        CMP #$11
        BEQ t8_b
        BRA t8_fail
t8_b:
        LDA $800061
        CMP #$22
        BEQ t8_c
        BRA t8_fail
t8_c:
        LDA $800062
        CMP #$33
        BEQ t8_d
        BRA t8_fail
t8_d:
        LDA $800063
        CMP #$44
        BEQ t8_pass
t8_fail:
        LDA #$00
        BRA t8_store
t8_pass:
        LDA #$01
t8_store:
        STA $800038

; ============================================================
; Aggregate: $800039 = $01 iff every test above passed
; ============================================================
agg:
        LDA $800030
        CMP #$01
        BEQ agg_1
        BRA agg_fail
agg_1:
        LDA $800031
        CMP #$01
        BEQ agg_2
        BRA agg_fail
agg_2:
        LDA $800032
        CMP #$01
        BEQ agg_3
        BRA agg_fail
agg_3:
        LDA $800033
        CMP #$01
        BEQ agg_4
        BRA agg_fail
agg_4:
        LDA $800034
        CMP #$01
        BEQ agg_5
        BRA agg_fail
agg_5:
        LDA $800035
        CMP #$01
        BEQ agg_6
        BRA agg_fail
agg_6:
        LDA $800036
        CMP #$01
        BEQ agg_7
        BRA agg_fail
agg_7:
        LDA $800037
        CMP #$01
        BEQ agg_8
        BRA agg_fail
agg_8:
        LDA $800038
        CMP #$01
        BEQ agg_pass
agg_fail:
        LDA #$00
        BRA agg_store
agg_pass:
        LDA #$01
agg_store:
        STA $800039

; ============================================================
; Visual indicator (CPU-only, grayscale): draw the word "PASS"
; as a 15x5-pixel bitmap iff every test passed. Left black
; otherwise.
;
; The rolling framebuffer only accepts writes to scanlines
; within a small window that tracks the beam (see CLAUDE.md's
; "Rolling Buffer" section / docs/display.md "No Random Access")
; - a fixed Y like $08 would already have rolled out of the
; window by the time ~250 prior instructions have executed, and
; the writes would be silently dropped. Instead this reads the
; live scanline counter ($D80040, DISP_SCANLINE low byte) and
; draws a few scanlines *ahead* of the beam, which is always
; in-window regardless of how long the test suite took to run.
; The chosen row is stashed at $800048 so a harness that wants
; to verify the pixels landed knows which scanline to check.
; Each glyph row re-reads/re-derives $800048 rather than the
; live beam directly, matching the original 3x3-block design;
; 5 rows of text still leaves comfortable margin inside the
; 16-scanline rolling window (see display-rolling-buffer-model
; memory).
; ============================================================
        LDA $800039
        CMP #$01
        BEQ draw_block
        BRA halt

draw_block:
        LDA #$0F
        STA $B00104          ; palette index = white, set once
        LDA #$00
        STA $B00101          ; X high = 0, set once
        STA $B00103          ; Y high = 0, set once

        LDA $D80040           ; current scanline (low byte)
        CLC
        ADC #$08                ; draw a few scanlines ahead of the beam -
                                 ; needs more margin than the old 3x3 block
                                 ; (34 pixels vs 9 takes longer to draw, so
                                 ; the beam is closer behind by the time row
                                 ; 0's writes land; +4 measurably dropped
                                 ; row 0, +8 verified clean - see
                                 ; diag-rom-tester memory)
        STA $800048              ; row 0 Y, also left here for a harness

        ; --- row 0: P***A*.S**.S** (top strokes) ---
        LDA $800048
        STA $B00102
        LDA #$08
        STA $B00100
        STA $B0010A
        LDA #$09
        STA $B00100
        STA $B0010A
        LDA #$0A
        STA $B00100
        STA $B0010A
        LDA #$0D
        STA $B00100
        STA $B0010A
        LDA #$11
        STA $B00100
        STA $B0010A
        LDA #$12
        STA $B00100
        STA $B0010A
        LDA #$15
        STA $B00100
        STA $B0010A
        LDA #$16
        STA $B00100
        STA $B0010A

        ; --- row 1 ---
        LDA $800048
        CLC
        ADC #$01
        STA $800048
        STA $B00102
        LDA #$08
        STA $B00100
        STA $B0010A
        LDA #$0A
        STA $B00100
        STA $B0010A
        LDA #$0C
        STA $B00100
        STA $B0010A
        LDA #$0E
        STA $B00100
        STA $B0010A
        LDA #$10
        STA $B00100
        STA $B0010A
        LDA #$14
        STA $B00100
        STA $B0010A

        ; --- row 2 ---
        LDA $800048
        CLC
        ADC #$01
        STA $800048
        STA $B00102
        LDA #$08
        STA $B00100
        STA $B0010A
        LDA #$09
        STA $B00100
        STA $B0010A
        LDA #$0A
        STA $B00100
        STA $B0010A
        LDA #$0C
        STA $B00100
        STA $B0010A
        LDA #$0D
        STA $B00100
        STA $B0010A
        LDA #$0E
        STA $B00100
        STA $B0010A
        LDA #$11
        STA $B00100
        STA $B0010A
        LDA #$15
        STA $B00100
        STA $B0010A

        ; --- row 3 ---
        LDA $800048
        CLC
        ADC #$01
        STA $800048
        STA $B00102
        LDA #$08
        STA $B00100
        STA $B0010A
        LDA #$0C
        STA $B00100
        STA $B0010A
        LDA #$0E
        STA $B00100
        STA $B0010A
        LDA #$12
        STA $B00100
        STA $B0010A
        LDA #$16
        STA $B00100
        STA $B0010A

        ; --- row 4 ---
        LDA $800048
        CLC
        ADC #$01
        STA $800048
        STA $B00102
        LDA #$08
        STA $B00100
        STA $B0010A
        LDA #$0C
        STA $B00100
        STA $B0010A
        LDA #$0E
        STA $B00100
        STA $B0010A
        LDA #$10
        STA $B00100
        STA $B0010A
        LDA #$11
        STA $B00100
        STA $B0010A
        LDA #$14
        STA $B00100
        STA $B0010A
        LDA #$15
        STA $B00100
        STA $B0010A

        ; $800048 now holds row 4's Y; back it up to row 0 for a harness
        LDA $800048
        SEC
        SBC #$04
        STA $800048

        ; Redraw once per frame instead of halting. The rolling framebuffer
        ; clears itself and re-latches every frame (Display::tick()'s
        ; wraparound - see display-rolling-buffer-model memory), so a
        ; one-shot draw gets wiped by the very next frame with nothing
        ; re-painting it. Looping straight back to draw_block (no wait)
        ; redraws using whatever scanline the beam happens to be at on each
        ; pass, which is a *different* value every time since the loop free-
        ; runs far faster than one frame - producing a trail of "PASS" text
        ; smeared down the screen instead of one stable copy. Instead, spin
        ; until the scanline counter wraps back to 0 (start of a new frame)
        ; before redrawing, so Y is always 0+8 - fixed and identical every
        ; frame. Only the PASS path loops; FAIL still halts immediately
        ; below since nothing is drawn on that path.
        ;
        ; DISP_SCANLINE is a 16-bit counter (0-260, since TOTAL_SCANLINES=261
        ; doesn't fit in 8 bits) split across $D80040 (low) / $D80041 (high).
        ; Checking only the low byte against 0 is a trap: scanline 256 ($0100)
        ; *also* has a low byte of $00, so a low-byte-only check fires almost
        ; a full frame late, at the tail of the current frame instead of the
        ; true start - exactly why the first rows written landed on a window
        ; that had already rolled past and got silently dropped. Check both
        ; bytes so only the genuine scanline-0 (frame start) passes.
wait_frame:
        LDA $D80040
        CMP #$00
        BEQ wait_frame_hi
        BRA wait_frame
wait_frame_hi:
        LDA $D80041
        CMP #$00
        BEQ draw_block
        BRA wait_frame

halt:
        .BYTE $FF             ; opcode 0xFF: HLT (cpuasm has no HLT mnemonic,
                               ; see cpuasm.c's instruction table - the
                               ; interpreter implements it, so it's emitted
                               ; directly; cpu.isHalted() then reports true)
