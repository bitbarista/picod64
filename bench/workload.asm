; ---------------------------------------------------------------------------
; PicoD64 feasibility workload -- a 16 KiB drive ROM image, $C000-$FFFF.
;
; OUR OWN CODE, written for this bench. It is NOT the 1541 DOS and copies none
; of it. It imitates the SHAPE of the DOS's busy loops so the emulator's cost
; per cycle is measured under realistic load:
;   - wait for SYNC on VIA2 PB7, then a BVC * / CLV / LDA $1C01 byte loop
;     driven by BYTE READY on SO, as a GCR read does
;   - a table-driven decode pass (indexed, (zp),Y, RMW, shifts)
;   - an IEC poll loop on VIA1 port B, toggling the outputs
;   - decimal-mode arithmetic, JSR/RTS, stack traffic
;   - a VIA2 timer-1 IRQ, free running, TIMER cycles apart
;
;   acme -DTIMER=10000 -o build/wl_dos.bin    workload.asm   (DOS-like)
;   acme -DTIMER=150   -o build/wl_stress.bin workload.asm   (IRQ storm)
; ---------------------------------------------------------------------------
!cpu 6502
!ifndef TIMER { TIMER = 10000 }

VIA1    = $1800
VIA2    = $1C00
PASSES  = $05           ; 16-bit main-loop pass counter
IRQS    = $08           ; 16-bit IRQ counter
SUM     = $11
PTR     = $20
BUF0    = $0300
BUF1    = $0400

        * = $C000
reset   sei
        cld
        ldx #$FF
        txs
        lda #0
        ldx #0
-       sta $00,x       ; clear zero page
        inx
        bne -
        ; VIA2: CA2 (SOE) and CB2 manual high, CA1 negative edge
        lda #$EE
        sta VIA2+$0C
        ; VIA2 PB: 0-3, 5, 6 out; 4 and 7 in. Motor on, zone 3 (bits 5-6 = 11)
        lda #$6F
        sta VIA2+$02
        lda #$64
        sta VIA2+$00
        ; VIA2 timer 1: free running, TIMER cycles, IRQ enabled
        lda #$40
        sta VIA2+$0B
        lda #<TIMER
        sta VIA2+$04
        lda #>TIMER
        sta VIA2+$05
        lda #$C0
        sta VIA2+$0E
        ; VIA1: PB1, PB3, PB4 out (DATA, CLK, ATNA), all released
        lda #$1A
        sta VIA1+$02
        lda #$00
        sta VIA1+$00
        lda #<BUF0
        sta PTR
        lda #>BUF0
        sta PTR+1
        cli

main    jsr read_sector
        jsr decode
        jsr iec_poll
        jsr dec_math
        inc PASSES
        bne +
        inc PASSES+1
+       jmp main

; --- wait for SYNC, then 256 + 70 bytes on BYTE READY ----------------------
read_sector
-       bit VIA2        ; PB7 = SYNC, low while in sync
        bmi -
        lda VIA2+$01    ; discard
        clv
        ldy #0
-       bvc -
        clv
        lda VIA2+$01
        sta BUF0,y
        iny
        bne -
        ldy #0
-       bvc -
        clv
        lda VIA2+$01
        sta BUF1,y
        iny
        cpy #70
        bne -
        rts

; --- table-driven decode pass ------------------------------------------------
decode  ldx #0
-       lda BUF0,x
        lsr
        lsr
        lsr
        tay
        lda table,y
        sta $10
        lda BUF0,x
        and #$07
        asl
        asl
        ora $10
        eor SUM
        sta SUM
        txa
        tay
        lda (PTR),y
        adc SUM
        sta BUF1,y
        rol $12
        inc $0200,x     ; abs,X RMW, clear of the pointers
        dex
        bne -
        rts

; --- IEC: poll the bus and toggle CLK/DATA out, as a byte handshake does -----
iec_poll
        ldx #200
-       lda VIA1
        and #$85        ; ATN in, CLK in, DATA in
        cmp #$85
        beq +
        lda VIA1
        eor #$08        ; toggle CLK out
        sta VIA1
+       dex
        bne -
        lda VIA1
        and #$F5
        sta VIA1        ; release CLK and DATA
        rts

; --- decimal arithmetic, nested calls ----------------------------------------
dec_math
        sed
        ldx #64
-       lda $30
        clc
        adc #$19
        sta $30
        lda $31
        sec
        sbc #$07
        sta $31
        jsr leaf
        dex
        bne -
        cld
        rts
leaf    pha
        txa
        pha
        pla
        tax
        pla
        rts

; --- VIA2 timer-1 IRQ ---------------------------------------------------------
irq     pha
        txa
        pha
        tya
        pha
        lda VIA2+$04    ; clears the T1 flag
        inc IRQS
        bne +
        inc IRQS+1
+       pla
        tay
        pla
        tax
        pla
        rti

nmi     rti

table   !for i, 0, 31 { !byte (i * 37 + 11) & $FF }

        * = $FFFA
        !word nmi, reset, irq
