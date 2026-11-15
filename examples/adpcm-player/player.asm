; player.asm - plays a stereo IMA ADPCM track, looping, by feeding the APU's own ADPCM decoder (tools/gen_adpcm_apu.py).
;
; The decoder runs on the APU (fast mode) and writes decoded frames into the MMP stream ring itself. This program is
; only the loader and the feeder: it uploads the APU program and its tables from ROM through the DMA controller,
; switches the APU to fast mode, points the APU's PC at the program ($0500) and then keeps the APU's 48-page input
; ring ($A0:3800-$A0:6FFF = 56 pages, one 256-byte page = 256 frames) full from the ADPCM data in ROM, wrapping the ROM pointer
; at the end of the track so it loops. Flow control is a one-byte mailbox each way: $A0:1000 = the page this program
; fills next (written after each page lands), $A0:1800 = the page the APU is reading. A page is only written when
; the page after it is not the one the APU is reading, so the ring is never full to the brim.
;
; Everything is 8-bit A/X (see zp_dma.asm for why). DB = $80 so the variables below are plain absolute addresses
; (cpuasm has no long CMP/INC, and zp_dma_load and the APU window use literal 24-bit addresses anyway).
;   $80:0100 wr page (0-55)   $80:0102 next page   $80:0103 the APU's read page   $80:0110/1 pages sent this loop
;   $80:0104 1 once the first trip round the track is done   $80:0120/1 next chunk to verify (signed ROMs)
;
; Signed (v3) ROMs: the ADPCM data is the data region, and a 16384-byte chunk (64 pages) reads as poison until
; COP #$FF has verified it (chunk index in X, 16-bit index mode; DB is not preserved by the real Boot ROM, so it is
; set again). VERIFY_ENABLED (adpcm_cfg.inc) turns it on; the first trip round the track verifies each chunk as its
; first page comes up, later trips skip it (the chunks stay verified). On the real Boot ROM a verify takes ~0.4 s,
; longer than the ring (0.32 s at 44.1 kHz, 0.6 s at 24 kHz) can cover, so only the HLE boot (a flat 64 cycles) keeps up at 44.1 kHz.
;   $800010-$800018 zp_dma_load's parameter block (src advances as it goes, so it is also our ROM read pointer)
.org $8000
.bank $00
.include "adpcm_cfg.inc"

start:
        SEP #$30
        SDB #$80

        ; --- upload the APU program to $A0:0200 and its tables to $A0:9000 ---
        LDA #$00
        STA $800010
        LDA #CODE_SRC_MID
        STA $800011
        LDA #CODE_SRC_BANK
        STA $800012
        LDA #$00
        STA $800013
        LDA #$02
        STA $800014
        LDA #$A0
        STA $800015
        LDA #$00
        STA $800016
        LDA #CODE_LEN_HI
        STA $800017
        LDA #$00
        STA $800018
        JSR zp_dma_load

        LDA #$00
        STA $800010
        LDA #TABLES_SRC_MID
        STA $800011
        LDA #TABLES_SRC_BANK
        STA $800012
        LDA #$00
        STA $800013
        LDA #$90
        STA $800014
        LDA #$A0
        STA $800015
        LDA #$00
        STA $800016
        LDA #TABLES_LEN_HI
        STA $800017
        LDA #$00
        STA $800018
        JSR zp_dma_load

        ; --- feeder state: the ROM read pointer at the start of the track, one page per DMA ---
        LDA #$00
        STA $0100
        STA $0104
        STA $0110
        STA $0111
        STA $0120
        STA $0121
        STA $800010
        STA $800011
        STA $800013
        STA $800016
        STA $800018
        LDA #DATA_BANK
        STA $800012
        LDA #$A0
        STA $800015
        LDA #$01
        STA $800017                ; len = 256 bytes

        ; the real Boot ROM leaves its verify result (1) in mailbox $1000: clear both mailboxes before the APU starts
        LDA #$00
        STA $A01000
        STA $A01800

        ; --- go: fast mode (after the uploads: DMA to the APU window runs at half rate in fast mode), start the program ---
        LDA #$01
        STA $D8001A
        LDA #$00
        STA $D80012
        LDA #$05
        STA $D80013                ; APU PC = $0500

feed_loop:
        LDA $0100
        CLC
        ADC #$01                   ; next = (wr + 1) mod 48
        CMP #IN_PAGES
        BEQ next_wrap
        BRA next_ok
next_wrap:
        LDA #$00
next_ok:
        STA $0102
        LDA $A01800                ; the page the APU is reading
        STA $0103
        LDA $0102
        CMP $0103
        BEQ feed_loop              ; filling wr would make the ring full: wait for the APU

        LDA #VERIFY_ENABLED
        BEQ no_verify
        LDA $0104
        BNE no_verify              ; second trip onward: every chunk is verified already
        LDA $800011
        AND #$3F
        BNE no_verify              ; not the first page of a chunk
        REP #$30
        LDA $0120
        TAX
        COP #$FF
        SEP #$30
        SDB #$80                   ; the real Boot ROM does not restore DB
        INC $0120
        BNE no_verify
        INC $0121
no_verify:
        LDA $0100
        CLC
        ADC #IN_PAGE0
        STA $800014                ; dst = $A0:IN_PAGE0 00 + wr * 256
        JSR zp_dma_load            ; blocks until the page has landed; the source pointer moves on by 256

        LDA $0102
        STA $0100
        STA $A01000                ; publish: the APU may now read up to the page before this one

        INC $0110
        BNE no_carry
        INC $0111
no_carry:
        LDA $0110
        CMP #TOTAL_PAGES_LO
        BNE feed_loop
        LDA $0111
        CMP #TOTAL_PAGES_HI
        BNE feed_loop

        ; end of the track: wrap the ROM pointer, the APU reloads its initial state at the same page
        LDA #$00
        STA $0110
        STA $0111
        STA $800010
        STA $800011
        LDA #DATA_BANK
        STA $800012
        LDA #$01
        STA $0104
        BRA feed_loop

.include "zp_dma.asm"
