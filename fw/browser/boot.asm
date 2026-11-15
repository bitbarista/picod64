; =============================================================================
; PicoD64 browser's bootstrap: what LOAD"*",8 gets, so it is on screen in a few seconds, not 16.
; About 300 bytes through the KERNAL; it then asks the firmware for the browser itself (XB) over
; the turbo (fw/bus.h bus_turbo) and runs it. A wrong checksum: LOAD"BROWSER",8 and RUN, typed.
; =============================================================================
!cpu 6502
!to "boot.prg", cbm
SETLFS = $FFBA
SETNAM = $FFBD
OPEN   = $FFC0
CLOSE  = $FFC3
CHKIN  = $FFC6
CHKOUT = $FFC9
CLRCHN = $FFCC
CHRIN  = $FFCF
CHROUT = $FFD2
READY  = $A474
FA     = $BA
NDX    = $C6
KEYD   = $0277
MAIN   = $080D          ; browser.asm's first instruction
STUB   = $033C

*= $0801
        !word next_line, 2026
        !byte $9E
        !text "2061"
        !byte 0
next_line:
        !word 0
start:
        ldx #stub_end-stub_start-1
-       lda stub_image,x
        sta STUB,x
        dex
        cpx #$FF                ; (it may be over 127 bytes: not bpl)
        bne -
        lda #2                  ; XB: the browser into the firmware's RAM
        ldx #<cmd_xb
        ldy #>cmd_xb
        jsr open15
        ldx #15
        jsr CHKIN
        ldy #0
-       sty yy
        jsr CHRIN
        ldy yy
        sta reply,y
        iny
        cmp #$0D
        beq +
        cpy #24
        bne -
+       lda #15
        jsr CLOSE
        jsr CLRCHN
        lda reply+3             ; "00,TURBO,0801,llll"
        cmp #$54
        beq +
        jmp fallback
+       ldx #14
        jsr hex2
        sta s_hi
        jsr hex2
        clc
        adc #$01                ; the end: $0801 + length
        sta s_lo
        lda s_hi
        adc #$08
        sta s_hi
        lda #0                  ; XG, with no name on the OPEN: its UNLISTEN is the last thing on the bus
        tax
        tay
        jsr open15
        ldx #15
        jsr CHKOUT
        lda #$58
        jsr CHROUT
        lda #$47
        jsr CHROUT
        jsr CLRCHN
        lda #0
        sta $98
        lda $DD00
        and #$07
        sta rx_rel+1
        ora #$20
        sta rx_pull+1
        lda $DD00
        lsr
        lsr
        eor $DD00
        lsr
        lsr
        eor $DD00
        lsr
        lsr
        eor $DD00
        eor #$FF
        sta rx_const+1
        lda #$01
        sta $AE
        lda #$08
        sta $AF
        jmp STUB

open15: jsr SETNAM
        lda #15
        ldx FA
        ldy #15
        jsr SETLFS
        jmp OPEN

hex2:   jsr hex1
        asl
        asl
        asl
        asl
        sta yy
        jsr hex1
        ora yy
        rts
hex1:   lda reply,x
        inx
        cmp #$41
        bcc +
        sbc #7
+       and #$0F
        rts

; LOAD"BROWSER",8 and RUN, typed
fallback:
        ldx #0
-       lda t_fb,x
        beq +
        jsr CHROUT
        inx
        bne -
+       lda #$0D
        sta KEYD
        sta KEYD+1
        lda #2
        sta NDX
        jmp READY
t_fb:   !byte $93,$11,$11,$4C,$4F,$41,$44,$22,$42,$52,$4F,$57,$53,$45,$52,$22,$2C,$38
        !byte $0D,$0D,$0D,$0D,$0D,$52,$55,$4E,$13,0

cmd_xb: !byte $58,$42
yy:     !byte 0
reply:  !fill 24, 0

stub_image:
!pseudopc STUB {
stub_start:
        sei
        lda $D011
        and #$EF
        sta $D011
        ldx #0
        ldy #20
-       dex
        bne -
        dey
        bne -
s_loop:
        lda $AE
s_lo = * + 1
        cmp #0
        bne +
        lda $AF
s_hi = * + 1
        cmp #0
        beq s_sum
+       jsr rx
        ldy #0
        sta ($AE),y
        eor s_x
        sta s_x
        inc $AE
        bne s_loop
        inc $AF
        jmp s_loop
s_sum:  jsr rx
        cmp s_x
        php
        lda $D011
        ora #$10
        sta $D011
        plp                     ; (first: it would put the I flag back)
        cli
        bne s_bad
        jmp MAIN
; a wrong byte: the bootstrap may be gone. The plain way needs only the ROM
s_bad:  ldx #0
-       lda s_fb,x
        beq +
        jsr $FFD2
        inx
        bne -
+       lda #$0D
        sta KEYD
        sta KEYD+1
        lda #2
        sta NDX
        jmp READY
s_fb:   !byte $93,$11,$11,$4C,$4F,$41,$44,$22,$42,$52,$4F,$57,$53,$45,$52,$22,$2C,$38
        !byte $0D,$0D,$0D,$0D,$0D,$52,$55,$4E,$13,0
rx:
rx_pull:
        lda #0
        sta $DD00
rx_rel:
        lda #0
        sta $DD00
        nop
        nop
        nop
        lda $DD00
        lsr
        lsr
        eor $DD00
        lsr
        lsr
        eor $DD00
        lsr
        lsr
        eor $DD00
rx_const:
        eor #0
        rts
s_x:    !byte 0
stub_end:
}
!if stub_end - stub_start > 192 { !error "the bootstrap's stub no longer fits the cassette buffer" }
