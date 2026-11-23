; =============================================================================
; PicoD64 line timing: the C64 side of serial O (fw/pico_main.c line_timing, fw/linetime.h).
; How late PicoD64's pulls and releases of CLK and DATA reach a real C64, as its CIA reads them.
;
; The C64 pulls one line (the trigger) and reads the other exactly 4 cycles later (STX then LDA,
; both on their 4th cycle). PicoD64 sees the trigger, waits a delay it chooses, then pulls or
; releases the other line. Seen by the read or not is told back by when the C64 lets go of the
; trigger: ~19 us after it if seen, ~125 us if not. PicoD64 steps its delay 6.7 ns at a time; the
; delay at which half the reads see it marks where the edge crosses the CIA's input threshold.
; The C64's 1 us cycle doesn't limit it: every trial is locked to the C64's own write.
;
; Trials alternate: A pulls DATA and reads CLK, B pulls CLK and reads DATA.
; Start/stop: PicoD64 pulls CLK and DATA together for 10 ms ("hello", then "done"); the
; screen is off during the trials (no bad lines between the write and the read).
; SPACE stops it. Load from the card and RUN, then serial O (either order).
;   sh fw/linetime/build.sh
; =============================================================================
!cpu 6502
!to "linetime.prg", cbm
CHROUT = $FFD2
PORT   = $DD00          ; CIA2 port A: 3 ATN out, 4 CLK out, 5 DATA out (1 = pull), 6 CLK in, 7 DATA in (1 = high)

base   = $FB            ; zero page: the port with all lines released
mask   = $FC
pre    = $FD
sv11   = $FE
sv15   = $02

*= $0801
        !word next_line, 2026
        !byte $9E
        !text "2061"
        !byte 0
next_line:
        !word 0

start:                  ; $080D
        ldx #0
-       lda msg_wait,x
        beq +
        jsr CHROUT
        inx
        bne -
+       sei
        lda #$7F
        sta $DC0D       ; no CIA interrupts (CIA2's would be an NMI)
        sta $DD0D
        lda $DC0D
        lda $DD0D
        lda PORT
        and #$07        ; VIC bank and RS-232 out kept; ATN, CLK, DATA released
        sta base
        sta PORT
        lda #$7F
        sta $DC00       ; keyboard row 7: SPACE is $DC01 bit 4

hello:  jsr space
        bcs quit
        jsr bothlow
        bcc hello
        lda $D011       ; PicoD64 is there: screen off, sprites off
        sta sv11
        and #$EF
        sta $D011
        lda $D015
        sta sv15
        lda #0
        sta $D015
-       bit $D011       ; to the next frame past line $30: no bad lines from here on
        bpl -
-       bit $D011
        bmi -
-       lda $D012
        cmp #$40
        bcc -
-       lda PORT        ; the hello over: both lines released
        and #$C0
        cmp #$C0
        bne -

trials: lda base
        ora #$20
        tax
        lda #$40
        jsr trial       ; A: DATA pulled, CLK read
        bcs done
        lda base
        ora #$10
        tax
        lda #$80
        jsr trial       ; B: CLK pulled, DATA read
        bcs done
        jsr space
        bcc trials

quit:   ldy #msg_quit - msgs
        !byte $2C       ; (BIT abs: skips the next LDY)
done:   ldy #msg_done - msgs
        lda base
        sta PORT
        lda sv11
        ora #$10
        sta $D011
        lda sv15
        sta $D015
        lda #$81
        sta $DC0D       ; the KERNAL's timer interrupt back
        cli
-       lda msgs,y
        beq +
        jsr CHROUT
        iny
        bne -
+       rts

; one trial: X = the port with the trigger pulled, A = the input bit to watch. C set: PicoD64 says done
trial:  sta mask
        ldy #30         ; ~150 us between trials: PicoD64 sets the line up again
-       dey
        bne -
        lda PORT
        and #$C0
        beq bye         ; both lines low: done?
        sta pre
        stx PORT        ; the trigger, on its 4th cycle
        lda PORT        ; the read, on its 4th cycle: 4 cycles after the pull
        eor pre
        and mask
        bne seen        ; changed: seen in time
        ldy #20         ; not seen: let go ~100 us later
-       dey
        bne -
seen:   lda base
        sta PORT
        clc
        rts
bye:    ; (falls into bothlow: C set if they stay low)

; C set: CLK and DATA both held low for ~3.3 ms
bothlow:
        ldy #0
-       lda PORT
        and #$C0
        bne +
        dey
        bne -
        sec
        rts
+       clc
        rts

space:  lda $DC01
        and #$10
        bne +
        sec
        rts
+       clc
        rts

msg_wait:
        !byte 13
        !pet "picod64 line timing: waiting for", 13
        !pet "picod64 (serial o). space stops.", 13, 0
msgs:
msg_done:
        !pet "done: the results are on picod64's", 13
        !pet "serial port.", 13, 0
msg_quit:
        !pet "stopped.", 13, 0
