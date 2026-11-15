; ---------------------------------------------------------------------------
; PicoD64 micro-benchmarks: the emulator's bare cost per cycle. OUR CODE.
;   -DMODE=0  NOP loop: 2-cycle implied ops and a JMP, no data accesses
;   -DMODE=1  RAM loop: LDA zp / STA abs / INC abs,X / LDA (zp),Y, no I/O
; No VIA is touched after reset, no IRQ, so no events and no syncs: what is
; left is the CPU core and the per-access hooks.
; ---------------------------------------------------------------------------
!cpu 6502
!ifndef MODE { MODE = 0 }
        * = $C000
reset   sei
        ldx #$FF
        txs
!if MODE = 0 {
loop    !for i, 1, 64 { nop }
        jmp loop
} else {
        lda #$00
        sta $20
        lda #$03
        sta $21
        ldx #0
        ldy #0
loop    lda $10
        sta $0300
        inc $0400,x
        lda ($20),y
        sta $11
        inx
        iny
        jmp loop
}
irq
nmi     rti
        * = $FFFA
        !word nmi, reset, irq
