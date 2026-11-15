; ---------------------------------------------------------------------------
; PicoD64 equivalence workload: the VIA timers and head zones, the hard way.
;
; OUR OWN CODE. Not a drive program; it exists to make the event-driven core
; (core/) and the per-cycle reference (host/ref/) disagree if they can. A
; pseudo-random generator picks, every round:
;   - VIA1 or VIA2; T1 one-shot or free-run; a latch value (incl. 0, 1, 2)
;   - a new latch written mid-period (T1L-L/H), or not
;   - ACR switched between one-shot and free-run mid-count, or not
;   - T2 loaded with a random count, IRQ enabled or not
;   - a random delay, then T1C-L/H and T2C-L/H read and stored to RAM
;   - VIA2 PB written with a random zone while a byte loop runs
; IRQ handler stores IFR and the counters at entry, acknowledges, and masks
; the timer IRQs again (see irq).
;
;   acme -o build/wl_timers.bin wl_timers.asm
; ---------------------------------------------------------------------------
!cpu 6502

VIA1    = $1800
VIA2    = $1C00
RND     = $02           ; 16-bit LFSR state
VIA     = $04           ; pointer to the chosen VIA
OUT     = $06           ; pointer into the log at $0200-$07FF
ROUNDS  = $05F0         ; 16-bit round counter (in RAM; fingerprinted)

        * = $C000
reset   sei
        cld
        ldx #$FF
        txs
        lda #$A5
        sta RND
        lda #$1C
        sta RND+1
        lda #$00
        sta OUT
        lda #$02
        sta OUT+1
        lda #$EE            ; VIA2: SOE and CB2 high
        sta VIA2+$0C
        lda #$6F
        sta VIA2+$02
        lda #$64            ; zone 3
        sta VIA2+$00
        cli

round   jsr rnd
        and #1              ; which VIA
        beq +
        lda #>VIA2
        bne ++
+       lda #>VIA1
++      sta VIA+1
        lda #0
        sta VIA
        ; IER: T1 and T2 on or off, at random
        jsr rnd
        and #$60
        ora #$80
        ldy #$0E
        sta (VIA),y
        lda #$7F
        ldy #$0D
        sta (VIA),y         ; clear all flags
        ; ACR: one-shot or free-run
        jsr rnd
        and #$40
        ldy #$0B
        sta (VIA),y
        ; latch: small values often, to hit 0/1/2 and back-to-back events
        jsr rnd
        and #$07
        bne +
        jsr rnd             ; 1 in 8: a full random 16-bit latch, high byte small
        and #$03
        tax
        jsr rnd
        jmp ++
+       ldx #0
        jsr rnd
        and #$3F
++      ldy #$04
        sta (VIA),y         ; T1L-L
        txa
        ldy #$05
        sta (VIA),y         ; T1C-H: load and start
        ; T2: random count, half the time
        jsr rnd
        bmi +
        ldy #$08
        sta (VIA),y
        lda #0
        ldy #$09
        sta (VIA),y
+       jsr delay
        ; mid-period: new latch, half the time
        jsr rnd
        bpl +
        and #$1F
        ldy #$06
        sta (VIA),y
        lda #0
        ldy #$07
        sta (VIA),y
+       ; mid-count: flip one-shot/free-run, a quarter of the time
        jsr rnd
        and #$03
        bne +
        ldy #$0B
        lda (VIA),y
        eor #$40
        sta (VIA),y
+       jsr delay
        ; read everything into the log
        ldy #$04
        lda (VIA),y
        jsr log
        ldy #$05
        lda (VIA),y
        jsr log
        ldy #$08
        lda (VIA),y
        jsr log
        ldy #$09
        lda (VIA),y
        jsr log
        ldy #$0D
        lda (VIA),y
        jsr log
        ; zone change while bytes are coming in
        jsr rnd
        and #$60
        ora #$04
        sta VIA2
        ldx #6
-       bvc -
        clv
        lda VIA2+$01
        jsr log
        dex
        bne -
        ; a quarter of rounds: SOE off (CA2 low), so BYTE READY must reach
        ; only VIA2's CA1 flag, not V. Poll the flag, and log P to see V.
        jsr rnd
        and #$03
        bne ++
        lda #$EC
        sta VIA2+$0C
        clv
        ldx #4
-       lda VIA2+$0D
        and #$02
        beq -
        lda VIA2+$01        ; clears CA1
        php
        pla
        jsr log
        dex
        bne -
        lda #$EE
        sta VIA2+$0C
++      inc ROUNDS
        bne +
        inc ROUNDS+1
+       jmp round

; --- a random delay, 0..255 loop passes --------------------------------------
delay   jsr rnd
        and #$0F
        bne +
        ; 1 in 16: wait past a full 65536-cycle wrap, so a silently wrapping
        ; one-shot reaches its next underflow (reload if ACR became free-run)
        ldy #64
--      ldx #0
-       dex
        bne -
        dey
        bne --
+       jsr rnd
        tax
        beq +
-       dex
        bne -
+       rts

; --- append A to the log ring at $0200-$05EF -----------------------------------
log     sty $10
        ldy #0
        sta (OUT),y
        inc OUT
        bne +
        inc OUT+1
        lda OUT+1
        cmp #$06
        bne +
        lda #$02
        sta OUT+1
+       ldy $10
        rts

; --- 16-bit Galois LFSR, result in A -------------------------------------------
rnd     lsr RND+1
        ror RND
        bcc +
        lda RND+1
        eor #$B4
        sta RND+1
+       lda RND
        eor RND+1
        rts

; --- IRQ: log both VIAs' IFRs and VIA2's T1 counter, then acknowledge all -------
irq     pha
        txa
        pha
        tya
        pha
        lda VIA1+$0D
        jsr log
        lda VIA2+$0D
        jsr log
        lda VIA2+$04
        jsr log
        lda #$7F
        sta VIA1+$0D
        sta VIA2+$0D
        ; ⚠ mask the timer IRQs once taken: free-run T1 with a small latch
        ; otherwise interrupts faster than this handler runs, and the first
        ; version did 9 rounds in 3M cycles. The flags still latch and are logged.
        lda #$60
        sta VIA1+$0E
        sta VIA2+$0E
        pla
        tay
        pla
        tax
        pla
        rti

nmi     rti

        * = $FFFA
        !word nmi, reset, irq
