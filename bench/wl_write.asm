; ---------------------------------------------------------------------------
; PicoD64 equivalence workload: switch read/write at ODD moments.
;
; OUR OWN CODE. The DOS switches CB2 (MODE) right after a BYTE READY, so a
; switch in mid-byte, where the head must first be brought up to date under
; the old mode, was never exercised, and a planted bug survived
; host/mutate.sh. This one writes a few bytes, switches back after a delay
; that changes each pass, logs every byte it then reads, and steps now and
; then. Run against host/equiv.c's "multi" disk.
;
;   acme -o build/wl_write.bin wl_write.asm
; ---------------------------------------------------------------------------
!cpu 6502
VIA2    = $1C00
OUT     = $06
DLY     = $08               ; delay before the switch, varies per pass
PAT     = $09               ; the byte written
N       = $0A

        * = $C000
reset   sei
        cld
        ldx #$FF
        txs
        lda #$EE            ; SOE high, CB2 high: read
        sta VIA2+$0C
        lda #$6F
        sta VIA2+$02
        lda #$62            ; zone 3, phase 2 (half-track 34)
        sta VIA2+$00
        lda #$00
        sta OUT
        sta DLY
        sta PAT
        lda #$02
        sta OUT+1
        ldy #0
        clv
pass    ; --- wait DLY loops (5 cycles each), then switch to write mid-byte
        ldx DLY
w1      dex
        bne w1
        lda #$FF
        sta VIA2+$03        ; port A out
        lda PAT
        sta VIA2+$01
        lda #$CE            ; CB2 low: write
        sta VIA2+$0C
        lda #5
        sta N
wr      bvc wr              ; write 5 bytes
        clv
        lda PAT
        sta VIA2+$01
        inc PAT
        dec N
        bne wr
        ; --- an odd delay, then back to read mid-byte
        ldx DLY
w2      dex
        bne w2
        lda #$EE
        sta VIA2+$0C
        lda #$00
        sta VIA2+$03        ; port A in
        ; --- read and log 24 bytes
        lda #24
        sta N
rd      bvc rd
        clv
        lda VIA2+$01
        sta (OUT),y
        iny
        bne +
        inc OUT+1
        lda OUT+1
        cmp #$08
        bne +
        lda #$02
        sta OUT+1
+       dec N
        bne rd
        ; --- next pass: a new delay; every 16th pass, one half-track in
        lda DLY
        clc
        adc #7
        sta DLY
        and #$0F
        bne pass
        lda VIA2
        tax
        and #$FC
        sta $11
        txa
        clc
        adc #1
        and #$03
        ora $11
        sta VIA2
        jmp pass

irq
nmi     rti

        * = $FFFA
        !word nmi, reset, irq
