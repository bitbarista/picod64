; ---------------------------------------------------------------------------
; PicoD64 equivalence workload: read EVERY byte of every revolution.
;
; OUR OWN CODE. The DOS-like workloads read only after a SYNC, so bytes in
; the gaps (and the one that straddles the track's wrap) were never seen; a
; planted bug there survived host/mutate.sh. This one reads each byte on
; BYTE READY into a 1.5 KiB log in RAM, which the fingerprint covers.
;
;   acme -o build/wl_scan.bin wl_scan.asm
; ---------------------------------------------------------------------------
!cpu 6502
VIA2    = $1C00
OUT     = $06

        * = $C000
reset   sei
        cld
        ldx #$FF
        txs
        lda #$EE            ; SOE high
        sta VIA2+$0C
        lda #$6F
        sta VIA2+$02
        lda #$64            ; zone 3
        sta VIA2+$00
        lda #$00
        sta OUT
        lda #$02
        sta OUT+1
        ldy #0
        clv
; ⚠ The loop must beat one byte time (26 cycles in zone 3) or bytes are
; skipped: the first version (~40 cycles, checksum and SYNC per byte) never
; read the wrap byte. Now: 19 cycles per byte, and SYNC once per 256 bytes.
loop    bvc loop
        clv
        lda VIA2+$01        ; the byte
        sta (OUT),y
        iny
        bne loop
        lda VIA2            ; SYNC in bit 7, once per page
        sta $10
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
