; ---------------------------------------------------------------------------
; PicoD64 F1d timing workload: IEC output edges under load.
;
; OUR OWN CODE. Toggles CLK and DATA out (VIA1 PB3, PB1) in patterns that
; stress the emulator's real-time sync, while the rest of the drive is busy:
;   0: one CLK toggle after a random delay
;   1: a burst of 8 DATA toggles, 6 cycles apart (the tightest a 6502 does)
;   2: 20 GCR bytes read on BYTE READY (head events), then a CLK toggle
;   3: a decode-like loop (indexed, RMW, shifts), then CLK and DATA together
; plus a VIA2 timer-1 IRQ every 1002 cycles. With -DATNIRQ=1 the VIA1 CA1
; (ATN) interrupt is enabled and I is clear, as in the DOS, so every
; instruction end is a sync point too.
;
;   acme -DATNIRQ=0 -o build/wl_iec.bin     wl_iec.asm
;   acme -DATNIRQ=1 -o build/wl_iecatn.bin  wl_iec.asm
; ---------------------------------------------------------------------------
!cpu 6502
!ifndef ATNIRQ { ATNIRQ = 0 }

VIA1    = $1800
VIA2    = $1C00
RND     = $02
PASSES  = $05           ; 16-bit, fingerprinted
IRQS    = $08           ; 16-bit, fingerprinted

        * = $C000
reset   sei
        cld
        ldx #$FF
        txs
        lda #0
        ldx #0
-       sta $00,x
        inx
        bne -
        lda #$A5
        sta RND
        lda #$1C
        sta RND+1
        lda #$EE            ; VIA2: SOE, CB2 high
        sta VIA2+$0C
        lda #$6F
        sta VIA2+$02
        lda #$64            ; motor on, zone 3
        sta VIA2+$00
        lda #$40            ; T1 free run, 1000 (+2) cycles, IRQ on
        sta VIA2+$0B
        lda #<1000
        sta VIA2+$04
        lda #>1000
        sta VIA2+$05
        lda #$C0
        sta VIA2+$0E
        lda #$1A            ; VIA1: DATA out, CLK out, ATNA
        sta VIA1+$02
        lda #$00
        sta VIA1
!if ATNIRQ {
        lda #$00            ; CA1 on the falling edge
        sta VIA1+$0C
        lda #$82            ; CA1 interrupt enabled
        sta VIA1+$0E
}
        cli

main    jsr rnd
        and #$03
        beq p0
        cmp #1
        beq p1
        cmp #2
        beq p2
        jmp p3

p0      jsr rnd             ; random delay, then one CLK edge
        tax
        beq +
-       dex
        bne -
+       jsr clk
        jmp next

p1      ldx #8              ; 8 DATA edges, 6 cycles apart
-       lda VIA1
        eor #$02
        sta VIA1
        dex
        bne -
        jmp next

p2      ldx #20             ; 20 bytes on BYTE READY
        clv
-       bvc -
        clv
        lda VIA2+$01
        sta $0300,x
        dex
        bne -
        jsr clk
        jmp next

p3      ldx #64             ; decode-like work
-       lda $0300,x
        lsr
        lsr
        eor $10
        sta $10
        inc $0400,x
        rol $11
        dex
        bne -
        lda VIA1            ; CLK and DATA in one write
        eor #$0A
        sta VIA1

next    inc PASSES
        bne +
        inc PASSES+1
+       jmp main

clk     lda VIA1
        eor #$08
        sta VIA1
        rts

rnd     lsr RND+1
        ror RND
        bcc +
        lda RND+1
        eor #$B4
        sta RND+1
+       lda RND
        rts

irq     pha
        lda VIA2+$04        ; acknowledge T1
        lda VIA1+$01        ; acknowledge CA1 (never fires on the bench)
        inc IRQS
        bne +
        inc IRQS+1
+       pla
        rti

nmi     rti

        * = $FFFA
        !word nmi, reset, irq
