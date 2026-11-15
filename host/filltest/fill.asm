; PicoD64 -- a self-checking load test (2026-11-07). 10 SYS2061, then this routine, then DATA_LEN bytes of a 16-bit
; Galois LFSR (taps $B4 in the high byte; the low byte after each step) from SEED. RUN regenerates the pattern and
; compares every byte: "OK" and the count, or "BAD AT $xxxx" at the first wrong one. A dropped, doubled, swapped or
; shifted byte fails, and so does a sector from another file (each has its own seed). Built by host/filltest/make.py:
;   acme -DSEED=n -DFILENO=n -o out.prg fill.asm   (with fill.dat, the data, beside it)
CHROUT = $FFD2
lo  = $FB
hi  = $FC
ptr = $FD                                   ; and $FE
* = $0801
        !word basic_end, 10
        !byte $9E
        !text "2061"
        !byte 0
basic_end:
        !word 0
start:                                      ; $080D = 2061
        ldx #0
-       lda msg_head,x
        beq +
        jsr CHROUT
        inx
        bne -
+       lda #FILENO + $30
        jsr CHROUT
        lda #<SEED
        sta lo
        lda #>SEED
        sta hi
        lda #<data
        sta ptr
        lda #>data
        sta ptr+1
        ldy #0
loop:   lsr hi
        ror lo
        bcc +
        lda hi
        eor #$B4
        sta hi
+       lda lo
        cmp (ptr),y
        bne bad
        inc ptr
        bne +
        inc ptr+1
+       lda ptr
        cmp #<data_end
        lda ptr+1
        sbc #>data_end
        bcc loop
        ldx #0
-       lda msg_ok,x
        beq +
        jsr CHROUT
        inx
        bne -
+       lda #>(data_end - data)
        jsr hex
        lda #<(data_end - data)
        jsr hex
        lda #$0D
        jmp CHROUT
bad:    ldx #0
-       lda msg_bad,x
        beq +
        jsr CHROUT
        inx
        bne -
+       lda ptr+1
        jsr hex
        lda ptr
        jsr hex
        lda #$0D
        jmp CHROUT
hex:    pha
        lsr
        lsr
        lsr
        lsr
        jsr dig
        pla
        and #$0F
dig:    cmp #10
        bcc +
        adc #6
+       adc #$30
        jmp CHROUT
msg_head: !byte $0D
        !text "FILL TEST FILE "
        !byte 0
msg_ok: !text ": OK, $"
        !byte 0
msg_bad: !text ": BAD AT $"
        !byte 0
data:
        !binary "fill.dat"
data_end:
