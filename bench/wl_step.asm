; ---------------------------------------------------------------------------
; PicoD64 equivalence workload: read bytes STRAIGHT AFTER every step.
;
; OUR OWN CODE. The DOS waits for a SYNC after stepping, so bytes framed
; with the previous track's bits (before the head meets a SYNC on the new
; one) were never observed, and two planted stepper bugs survived
; host/mutate.sh. This one logs every byte, as wl_scan does, and every 32
; bytes steps one half-track: 70 in, 70 out, repeated, changing zone too.
; Run against host/equiv.c's "multi" disk (a different track per half-track).
;
;   acme -o build/wl_step.bin wl_step.asm
; ---------------------------------------------------------------------------
!cpu 6502
VIA2    = $1C00
OUT     = $06
DIR     = $08               ; 1 = in, $FF = out
CNT     = $09               ; steps left in this direction

        * = $C000
reset   sei
        cld
        ldx #$FF
        txs
        lda #$EE            ; SOE high
        sta VIA2+$0C
        lda #$6F
        sta VIA2+$02
        lda #$62            ; zone 3, phase 2 (the head rests on half-track 34)
        sta VIA2+$00
        lda #$00
        sta OUT
        lda #$02
        sta OUT+1
        lda #1
        sta DIR
        lda #70
        sta CNT
        ldy #0
        clv
loop    bvc loop
        clv
        lda VIA2+$01        ; the byte
        sta (OUT),y
        iny
        tya
        and #$1F
        bne loop
        ; --- one half-track, at once, then straight back to reading
        lda VIA2
        sta $10             ; SYNC and the rest of PB, logged
        tax
        and #$FC
        sta $11
        txa
        clc
        adc DIR
        and #$03
        ora $11
        eor #$20            ; and flip a zone bit: rezone and step together
        sta VIA2
        dec CNT
        bne page
        lda #70
        sta CNT
        lda DIR
        eor #$FE            ; 1 <-> $FF
        sta DIR
page    tya
        bne loop
        inc OUT+1
        lda OUT+1
        cmp #$08
        bne loop
        lda #$02
        sta OUT+1
        jmp loop

irq
nmi     rti

        * = $FFFA
        !word nmi, reset, irq
