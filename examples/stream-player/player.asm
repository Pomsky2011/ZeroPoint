; player.asm - streams stereo 8-bit PCM from cartridge ROM through the MMP stream port ($A0:0070-$A0:007F).
;
; The audio sits in ROM from bank AUDIO_BANK onward (stream_cfg.inc, written by tools/build_stream_rom.py). The
; ring lives in APU RAM at $3000-$6FFF (all of ARAM): 8192 frames (L,R bytes) = 32 pages of 256 frames, 0.62 s at
; 13125 Hz. This program only ever writes whole pages, so the write pointer's low byte is always 0, and it keeps 2 pages clear of the mixer's read
; position so a full ring can't look like an empty one (rd == wr means empty).
;
; Everything runs in 8-bit A/X (see zp_dma.asm for why: cpuasm has no 16-bit #imm, SBC/CMP have no long form, no
; BCC). Work RAM: $800100 = write page (0-31), $800101 = prefill counter, $800110/1 = next chunk index. zp_dma_load's
; own parameter block is $800010-$800018: its src pointer advances as it goes, so it doubles as our ROM read pointer.
;
; Signed (v3) ROMs: the audio is the data region, and a 16384-byte chunk reads as poison until the Boot ROM has
; verified it (COP #$FF, chunk index in X, 16-bit index mode). VERIFY_ENABLED (stream_cfg.inc) turns that on; leave
; it 0 for unsigned ROMs, where COP #$FF would be an ordinary COP. Chunk i starts at audio offset i*16384, i.e.
; every 32nd page: the DMA read pointer's middle byte is a multiple of $40. One verify is ~0.4 s of CPU time
; (BLAKE2s over 16 KB) against 0.62 s of audio per chunk: it only stalls the CPU, and the ring (full when the
; chunk boundary comes up, ~0.58 s) rides it out. A smaller ring underruns once per chunk.
.org $8000
.bank $00
.include "stream_cfg.inc"

start:
        SEP #$30
        SDB #$00

        ; --- stream port: BASE=$3000, SIZE=$2000 frames, RESET, PITCH, VOL=255 (still disabled) ---
        LDA #$00
        STA $A00072
        LDA #$30
        STA $A00073
        LDA #$00
        STA $A00074
        LDA #$20
        STA $A00075
        LDA #$02
        STA $A00070              ; RESET: RDPTR = WRPTR = 0
        LDA #PITCH_LO
        STA $A0007A
        LDA #PITCH_HI
        STA $A0007B              ; pitch applies on the high-byte write
        LDA #$FF
        STA $A0007C

        ; --- DMA parameter block: src = start of audio, len = 512 (one page), channel 0 ---
        LDA #$00
        STA $800010
        STA $800011
        LDA #AUDIO_BANK
        STA $800012
        LDA #$00
        STA $800016
        LDA #$02
        STA $800017
        LDA #$00
        STA $800018

        LDA #$00
        STA $800100              ; write page = 0
        STA $800110
        STA $800111              ; next chunk to verify = 0

        ; --- prefill 16 pages before the mixer starts ---
        LDA #$10
        STA $800101
prefill:
        JSR feed_page
        LDA $800101
        SEC
        SBC #$01
        STA $800101
        BEQ prefill_done
        BRA prefill
prefill_done:
        LDA #$01
        STA $A00070              ; ENABLE

main:
        ; --- audio exhausted? (src >= AUDIO_END) ---
        LDA $800012
        CMP #AUDIO_END_BANK
        BEQ check_mid
        BCS finished             ; bank past the end
        BRA check_room
check_mid:
        LDA $800011
        CMP #AUDIO_END_MID
        BCS finished

check_room:
        ; --- d = (read page - write page) & 31; refill only while at least 2 pages are free ---
        LDA $800100
        TAX                      ; X = write page
        BEQ load_rd              ; nothing to subtract
        LDA $A00077
        AND #$1F                 ; A = read page
sub_loop:
        SEC
        SBC #$01
        DEX
        BEQ sub_done
        BRA sub_loop
load_rd:
        LDA $A00077
sub_done:
        AND #$1F
        CMP #$02
        BCS refill
        BRA main                 ; ring is full enough: poll again
refill:
        JSR feed_page
        BRA main

finished:
        BRA finished             ; the ring drains, then the mixer reports UNDERRUN and goes quiet

; feed_page: DMA the next 512 ROM bytes into the page at the write pointer, then publish it (WRPTR = next page).
feed_page:
        LDA #VERIFY_ENABLED
        BEQ fp_no_verify
        LDA $800011
        AND #$3F
        BEQ fp_verify            ; first page of a chunk: have the Boot ROM check it before we read it
        BRA fp_no_verify
fp_verify:
        REP #$30
        LDA $800110
        TAX                      ; X = chunk index (16-bit)
        COP #$FF
        SEP #$30
        SDB #$00                 ; the service call doesn't restore DB
        LDA $800110
        CLC
        ADC #$01
        STA $800110
        BCS fp_idx_carry
        BRA fp_no_verify
fp_idx_carry:
        LDA $800111
        CLC
        ADC #$01
        STA $800111
fp_no_verify:
        LDA $800100
        ASL                      ; page * 512 bytes = page * 2 in the middle address byte
        CLC
        ADC #$30                 ; + ring base $3000
        STA $800014
        LDA #$00
        STA $800013
        LDA #$A0
        STA $800015              ; dst = $A0:3000 + page*512 (APU window)
        JSR zp_dma_load          ; blocks until the page has landed; advances src by 512

        LDA $800100
        CLC
        ADC #$01
        AND #$1F
        STA $800100
        LDA #$00
        STA $A00078              ; WRPTR low (always 0: whole pages)
        LDA $800100
        STA $A00079              ; WRPTR high commits
        RTS

.include "zp_dma.asm"
