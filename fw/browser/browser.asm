; =============================================================================
; PicoD64 browser: a file browser for the C64, built into the PicoD64 firmware.
; An original program (GPL-3.0-only), written from Commodore's KERNAL documentation
; and PicoD64's own command set; no code from any other browser.
;
;   sh fw/browser/build.sh
;
; Designed for 40 x 25 and no mouse (2026-11-02): the screen always says what the
; keys do, and there are few to remember:
;   CRSR           move (left/right: a page)
;   RETURN         what the footer says: run, or open
;   DEL            back; while filtering, removes a letter
;   RUN/STOP       clears the filter; otherwise quits to BASIC
;   typing         filters the list
;   F1             options for this item, when the footer offers them
;   F3             turbo on/off
;   Joystick 2     up/down move, left back, right or a tap of fire = RETURN, holding fire = F1
;
; It reads the directory with plain KERNAL calls, so it works on any drive. A program is
; launched as a person would: LOAD"name",8,1 and RUN typed through the keyboard buffer,
; so it behaves exactly as if typed. On PicoD64 the turbo is used instead, unless it is off:
; the firmware has the file in RAM and sends it (fw/bus.h bus_turbo). F3 turns it off for good
; measure (copy-protected titles); the F1 menu has RUN SLOW for one program. Any other answer
; from the drive, a file that would overwrite the turbo's code, or a wrong checksum: a plain LOAD.
; =============================================================================
!cpu 6502
!to "browser.prg", cbm

; KERNAL [C64 Programmer's Reference Guide, ch. 5]
READST = $FFB7
SETLFS = $FFBA
SETNAM = $FFBD
OPEN   = $FFC0
CLOSE  = $FFC3
CHKIN  = $FFC6
CHKOUT = $FFC9
CLRCHN = $FFCC
CHRIN  = $FFCF
CHROUT = $FFD2
GETIN  = $FFE4
READY  = $A474          ; BASIC warm start
FA     = $BA            ; the device last used
NDX    = $C6            ; keys in the keyboard buffer
KEYD   = $0277          ; the keyboard buffer
JIFFY  = $A2

SCREEN = $0400
COLOUR = $D800
ROW_TITLE  = 0
ROW_PATH   = 1
ROW_RULE1  = 2
LIST_ROW   = 3          ; first row of the list
LIST_H     = 19         ; rows of it
ROW_RULE2  = 22
ROW_FOOT   = 23
ROW_FILTER = 24

TABLE  = $3000          ; the entries, 64 bytes each, up to $9F00 (BASIC's ROM starts at $A000)
MAXENT = 445            ; 64 bytes each: a bigger folder is paged through (the firmware holds the list)
VIS    = $C000          ; the list as shown: 2 bytes per row, an entry's number

; an entry
E_NAME = 0              ; 46 bytes: ASCII from PicoD64's listing, PETSCII from a drive's
E_LEN  = 46
E_TYPE = 47             ; T_*
E_BLK  = 48             ; 2 bytes
E_TTXT = 50             ; 3 bytes: the format, as shown: D64, PRG, CRT...
E_DLEN = 53             ; the name's length as shown, without its extension
E_IDX  = 54             ; 2 bytes: its number in the firmware's list
T_PRG   = 0
T_DIR   = 1
T_OTHER = 2
T_UP    = 3
T_DISK  = 4
T_NOTHING = 5           ; (the footer's, when the list is empty)

; colours
C_TEXT  = 15            ; light grey
C_DIM   = 11            ; dark grey
C_HEAD  = 1             ; white
C_PRG   = 1             ; white
C_DIR   = 7             ; yellow
C_DISK  = 13            ; light green
C_SEL   = 1
NAME_W  = 28            ; columns for a name: a longer one shows its start and end

; keys
K_DOWN = $11
K_UP   = $91
K_RGHT = $1D
K_LEFT = $9D
K_RET  = $0D
K_DEL  = $14
K_STOP = $03
K_HOME = $13
K_F1   = $85
K_F3   = $86

PATHMAX = 100

; zero page: free for programs [PRG memory map]
ptr  = $FB
sptr = $FD
num  = $F7
vp   = $F9

*= $0801
        !word next_line, 2026
        !byte $9E
        !text "2061"
        !byte 0
next_line:
        !word 0

; ---------------------------------------------------------------------------
start:
        lda #0
        sta $D020
        sta $D021
        sta pathlen
        sta pathover
        sta lastlen
        sta fwcap
        sta fwmode
        lda #$17                ; the upper and lower case set: names as they are on the card
        sta $D018
        lda #$80                ; (and no switching it with SHIFT + C=)
        sta $0291
        lda #1
        sta turbo_on
        lda FA
        cmp #8
        bcs +
        lda #8
        sta FA
+       jsr frame
        jsr copy_stub
        ; PicoD64? XR gives the settings. Anything else is a plain drive.
        jsr try_xr
        bcc reload
        ; a plain drive: from the root if it has one
        lda #$43
        sta cmdbuf
        lda #$44
        sta cmdbuf+1
        lda #$2F
        sta cmdbuf+2
        sta cmdbuf+3
        lda #4
        jsr send_cmd
        ; A fast-load cartridge may have brought us here off PicoD64's temporary disk, with its 1541
        ; running: the CD// just sent puts it back to browsing. Then XR gets its answer. About a second
        ; here on a real 1541, once.
        lda #4
        sta m_r
-       lda JIFFY
        clc
        adc #15
        sta j_start
--      lda JIFFY
        cmp j_start
        bne --
        jsr try_xr
        bcc reload
        dec m_r
        bne -

reload:
        lda #0
        sta flen
        lda #$FF                ; (the window round the entry we came back to)
        sta w_want
        sta w_want+1
        jsr read_dir
        jsr rebuild
        jsr find_last
        jsr cur_from_sel
loop:
        jsr window_check
        jsr scroll
        jsr draw
wait:
        jsr GETIN
        bne key
        jsr joy
        beq wait
key:
        cmp #K_DOWN
        bne +
        lda #1
        jsr add_cur
        jmp loop
+       cmp #K_UP
        bne +
        lda #1
        jsr sub_cur
        jmp loop
+       cmp #K_RGHT
        bne +
        lda #LIST_H
        jsr add_cur
        jmp loop
+       cmp #K_LEFT
        bne +
        lda #LIST_H
        jsr sub_cur
        jmp loop
+       cmp #K_RET
        bne +
        jmp do_return
+       cmp #K_DEL
        bne +
        jmp do_del
+       cmp #K_STOP
        bne +
        jmp do_stop
+       cmp #K_F1
        bne +
        jmp do_menu
+       cmp #K_F3
        bne +
        lda turbo_on
        eor #1
        sta turbo_on
        jmp loop
+       cmp #K_HOME
        bne +
        lda #0
        sta cur
        sta cur+1
        jmp loop
+       jsr filter_char
        bcs +
        jmp wait
+       lda big
        beq +
        jsr server_filter
        jmp loop
+       jsr filter_narrow
        jmp loop

; cur += A, stopping at the last row
add_cur:
        clc
        adc cur
        sta cur
        bcc +
        inc cur+1
+       lda cur
        cmp nvis
        lda cur+1
        sbc nvis+1
        bcc +
        jsr cur_last
+       rts
cur_last:
        sec
        lda nvis
        sbc #1
        sta cur
        lda nvis+1
        sbc #0
        sta cur+1
        bcs +
        lda #0
        sta cur
        sta cur+1
+       rts

; cur -= A, stopping at 0
sub_cur:
        sta num
        sec
        lda cur
        sbc num
        sta cur
        lda cur+1
        sbc #0
        sta cur+1
        bcs +
        lda #0
        sta cur
        sta cur+1
+       rts

; keep cur on screen
scroll:
        lda cur                 ; cur < top: top = cur
        cmp top
        lda cur+1
        sbc top+1
        bcs +
        lda cur
        sta top
        lda cur+1
        sta top+1
        rts
+       clc                     ; cur >= top + LIST_H: top = cur - LIST_H + 1
        lda top
        adc #LIST_H
        sta num
        lda top+1
        adc #0
        sta num+1
        lda cur
        cmp num
        lda cur+1
        sbc num+1
        bcc +
        sec
        lda cur
        sbc #LIST_H-1
        sta top
        lda cur+1
        sbc #0
        sta top+1
+       rts

; ptr = TABLE + (X/A = lo/hi entry number) * 64
entry_ptr:
        stx ptr
        sta ptr+1
        ldx #6
-       asl ptr
        rol ptr+1
        dex
        bne -
        lda ptr+1
        clc
        adc #>TABLE
        sta ptr+1
        rts

; ptr = the entry on row X/A of the list
vis_entry:
        stx vp
        sta vp+1
        asl vp
        rol vp+1
        lda vp+1
        clc
        adc #>VIS
        sta vp+1
        ldy #0
        lda (vp),y
        tax
        iny
        lda (vp),y
        jmp entry_ptr

; the selected entry: ptr, its type in A; C set if the list is empty
selected:
        lda nvis
        ora nvis+1
        bne +
        sec
        rts
+       ldx cur
        lda cur+1
        jsr vis_entry
        ldy #E_TYPE
        lda (ptr),y
        clc
        rts

; ---------------------------------------------------------------------------
; the filter

; is A a character for the filter? C set (and it is added) if so; shifted letters as plain ones
filter_char:
        cmp #$C1
        bcc +
        cmp #$DB
        bcs fc_no
        and #$7F
+       cmp #$41
        bcc +
        cmp #$5B
        bcc fc_yes
+       cmp #$30
        bcc +
        cmp #$3A
        bcc fc_yes
+       cmp #$20
        beq fc_yes
        cmp #$2E
        beq fc_yes
        cmp #$2D
        beq fc_yes
fc_no:  clc
        rts
fc_yes:
        ldx flen
        cpx #16
        bcs fc_no
        sta fbuf,x
        inc flen
        sec
        rts

; a letter was added: the rows shown can only get fewer, so filter those (quick on a long list)
filter_narrow:
        lda #0
        sta f_from
        sta f_from+1
        sta f_to
        sta f_to+1
fn_loop:
        lda f_from
        cmp nvis
        lda f_from+1
        sbc nvis+1
        bcc +
        jmp filter_done
+       ldx f_from
        lda f_from+1
        jsr vis_entry
        ldy #0
        lda (vp),y
        sta f_val
        iny
        lda (vp),y
        sta f_val+1
        ora f_val
        beq fn_next             ; entry 0, back, is not a match
        jsr name_matches
        bcc fn_next
        jsr vis_store
fn_next:
        inc f_from
        bne fn_loop
        inc f_from+1
        jmp fn_loop

; VIS[f_to++] = f_val
vis_store:
        lda f_to
        sta vp
        lda f_to+1
        sta vp+1
        asl vp
        rol vp+1
        lda vp+1
        clc
        adc #>VIS
        sta vp+1
        ldy #0
        lda f_val
        sta (vp),y
        iny
        lda f_val+1
        sta (vp),y
        inc f_to
        bne +
        inc f_to+1
+       rts

; the whole list, through the filter (entry 0, back, only unfiltered and away from the root)
rebuild:
        lda #0
        sta f_to
        sta f_to+1
        sta f_val
        sta f_val+1
        lda flen
        bne rb_skip0
        lda pathlen
        bne rb_loop
rb_skip0:
        inc f_val
rb_loop:
        lda f_val
        cmp count
        lda f_val+1
        sbc count+1
        bcs filter_done
        lda flen
        beq rb_keep
        lda big                 ; (the firmware has already filtered a big folder)
        bne rb_keep
        ldx f_val
        lda f_val+1
        jsr entry_ptr
        jsr name_matches
        bcc rb_next
rb_keep:
        jsr vis_store
rb_next:
        inc f_val
        bne rb_loop
        inc f_val+1
        jmp rb_loop

filter_done:
        lda f_to
        sta nvis
        lda f_to+1
        sta nvis+1
        lda #0
        sta cur
        sta cur+1
        sta top
        sta top+1
        ; a back row is on the list: start on the first real item, with BACK one row above it
        ; (so a quick RETURN doesn't go straight back out)
        lda flen
        bne +
        lda pathlen
        beq +
        lda nvis+1
        bne ++
        lda nvis
        cmp #2
        bcc +
++      inc cur
+       rts

; does the entry at ptr's shown name contain the filter? C set if so. Case folded.
name_matches:
        ldy #E_LEN              ; (the extension counts: "game.crt" finds it)
        lda (ptr),y
        sec
        sbc flen
        bcc nm_no
        sta nm_last
        lda #0
        sta nm_at
nm_try:
        ldx #0
        ldy nm_at
nm_c:   lda (ptr),y
        jsr fold_up
        cmp fbuf,x
        bne nm_next
        iny
        inx
        cpx flen
        bne nm_c
        sec
        rts
nm_next:
        lda nm_at
        cmp nm_last
        beq nm_no
        inc nm_at
        bne nm_try
nm_no:  clc
        rts

; a letter of either alphabet (ASCII a-z, PETSCII $C1-$DA) as $41-$5A
fold_up:
        cmp #$61
        bcc +
        cmp #$7B
        bcs +
        sec
        sbc #$20
        rts
+       cmp #$C1
        bcc +
        cmp #$DB
        bcs +
        and #$7F
+       rts

; after going back: the cursor on the folder or disk we came from
find_last:
        lda lastlen
        beq fl_out
        lda #0
        sta f_from
        sta f_from+1
fl_loop:
        lda f_from
        cmp nvis
        lda f_from+1
        sbc nvis+1
        bcs fl_done
        ldx f_from
        lda f_from+1
        jsr vis_entry
        ldy #E_LEN
        lda (ptr),y
        cmp lastlen
        bne fl_next
        ldy #0
-       lda (ptr),y
        cmp lastname,y
        bne fl_next
        iny
        cpy lastlen
        bne -
        lda f_from
        sta cur
        lda f_from+1
        sta cur+1
        jmp fl_done
fl_next:
        inc f_from
        bne fl_loop
        inc f_from+1
        jmp fl_loop
fl_done:
        lda #0
        sta lastlen
fl_out:
        rts

; ---------------------------------------------------------------------------
; the joystick in port 2: a key code in A (Z clear), or 0 (Z set)
; a tap of fire is RETURN, half a second's hold is F1; up/down repeat, getting faster
joy:
        lda $DC00
        eor #$FF
        and #$1F
        sta j_now
        and #$10
        beq j_nofire
        lda j_fire              ; fire down
        bne +
        lda #1                  ; just pressed
        sta j_fire
        lda JIFFY
        sta j_start
        lda #0
        rts
+       cmp #1
        bne j_zero              ; (already acted)
        lda JIFFY
        sec
        sbc j_start
        cmp #30
        bcc j_zero
        lda #2
        sta j_fire
        lda #K_F1
        rts
j_nofire:
        lda j_fire
        ldx #0
        stx j_fire
        cmp #1
        bne j_dir
        lda #K_RET              ; a tap
        rts
j_dir:
        lda j_now
        bne +
        sta j_held              ; nothing pressed
j_zero: lda #0
        rts
+       lda j_held
        bne j_rep
        lda #24                 ; a new press acts at once; first repeat after 0.4 s
        clc
        adc JIFFY
        sta j_time
        lda #1
        sta j_held
        jmp j_act
j_rep:  lda JIFFY               ; repeat once the time is REACHED: an exact match was missed whenever the redraw after a
        sec                     ; step took longer than the interval, so on a real C64 holding the stick didn't scroll
        sbc j_time              ; (2026-11-08). Signed: up to 2 s late still counts
        bmi j_zero
        lda j_now
        and #3                  ; only up and down repeat
        beq j_zero
        lda #5                  ; then quickly
        clc
        adc JIFFY
        sta j_time
j_act:  lda j_now
        lsr
        bcc +
        lda #K_UP
        rts
+       lsr
        bcc +
        lda #K_DOWN
        rts
+       lsr
        bcc +
        lda #K_DEL              ; left: back
        rts
+       lsr
        bcc j_zero
        lda #K_RET              ; right: open
        rts

; ---------------------------------------------------------------------------
do_return:
        jsr selected
        bcc +
        jmp wait
+       cmp #T_UP
        beq go_back
        cmp #T_DIR
        bne *+5
        jmp go_in
        cmp #T_DISK
        bne *+5
        jmp go_in
        cmp #T_PRG
        bne +
        lda #1                  ; run it
        jmp launch
+       jmp wait                ; a SEQ, REL...: nothing to do

; DEL: a letter off the filter, or back
do_del:
        lda flen
        beq go_back
        dec flen
        jsr rebuild
        jmp loop

; RUN/STOP: the filter off, or to BASIC
do_stop:
        lda flen
        beq +
        lda #0
        sta flen
        jsr rebuild
        jmp loop
+       jmp quit

; up a folder, or out of a D64 (the firmware sees CD_ on the bus)
go_back:
        lda pathlen
        bne +
        jmp wait                ; at the root
+       lda vdisk               ; a disk host/try-vice.py put in the drive
        beq +
        ldx #2
        lda #0
        sta vdisk
        jsr bridge
        jmp ++
+       jsr send_cdup
        jsr wait_fw
++      jsr path_pop
        jmp reload

; after CD_: the drive comes off the bus, its written tracks go back to the card first. Until
; browse mode answers XR, commands would go to the drive's DOS
wait_fw:
        lda fwcap
        beq wf_out
        lda #60
        sta wf_n
wf_try: lda #$58
        sta cmdbuf
        lda #$52
        sta cmdbuf+1
        lda #2
        jsr cmd_reply
        ldy #6
-       lda t_reply,y
        cmp t_cfg,y
        bne +
        dey
        bpl -
wf_out: rts
+       ldx #0                  ; a moment, and again
        ldy #16
-       dex
        bne -
        dey
        bne -
        dec wf_n
        bne wf_try
        rts

send_cdup:
        lda #$43
        sta cmdbuf
        lda #$44
        sta cmdbuf+1
        lda #$5F
        sta cmdbuf+2
        lda #3
        jmp send_cmd

; into a folder or D64
go_in:
        lda #0
        sta br_fail
        jsr cd_selected
        lda br_fail
        beq +
        jmp wait
+       jsr path_push
        jmp reload

; on PicoD64 by its number (the name may be long); on a drive, CD:name
cd_selected:
        lda fwmode
        beq +
        lda #$4F                ; XO:hhhh
        jsr cmd_x_idx
        jmp send_cmd
+       lda #$43
        sta cmdbuf
        lda #$44
        sta cmdbuf+1
        lda #$3A
        sta cmdbuf+2
        ldy #E_LEN
        lda (ptr),y
        sta num
        ldy #0
-       cpy num
        beq +
        lda (ptr),y
        sta cmdbuf+3,y
        iny
        bne -
+       tya
        clc
        adc #3
        ldx hostfs              ; a D64 in a PC's folder: only host/try-vice.py can put it in a drive
        beq +
        pha
        ldy #E_TYPE
        lda (ptr),y
        cmp #T_DISK
        bne ++
        pla
        ldx #1
        jsr bridge
        bcs cd_no
        lda #1
        sta vdisk
        rts
cd_no:  lda #1
        sta br_fail
        rts
++      pla
+       jmp send_cmd

; asks host/try-vice.py, which watches this memory: X = 1 a disk in the drive (cmdbuf, length A,
; pathbuf the folder), 2 the folder back. Nothing else uses it. C set if nothing answered
MBOX = $03F0
bridge:
        sta MBOX+3
        lda #<cmdbuf
        sta MBOX+4
        lda #>cmdbuf
        sta MBOX+5
        lda pathlen
        sta MBOX+6
        lda #<pathbuf
        sta MBOX+7
        lda #>pathbuf
        sta MBOX+8
        stx MBOX+2
        lda #$50                ; PD
        sta MBOX
        lda #$44
        sta MBOX+1
        lda #2                  ; about 8.5 s
        sta m_r
        ldx #0
-       lda MBOX+2
        beq br_ok
        lda JIFFY
        cmp j_start
        beq -
        sta j_start
        inx
        bne -
        dec m_r
        bne -
        lda #0
        sta MBOX
        sec
        rts
br_ok:  sta MBOX
        clc
        rts

; cmdbuf = X<A>:hhhh, the selected entry's number; its length in A
cmd_x_idx:
        sta cmdbuf+1
        lda #$58
        sta cmdbuf
        lda #$3A
        sta cmdbuf+2
        ldx #3
        ; fall through
put_idx:
        ldy #E_IDX+1
        lda (ptr),y
        jsr put_hex
        ldy #E_IDX
        lda (ptr),y
        jsr put_hex
        txa
        rts
; A as two hex digits at cmdbuf,x
put_hex:
        pha
        lsr
        lsr
        lsr
        lsr
        jsr put_dig
        pla
        and #$0F
put_dig:
        cmp #10
        bcc +
        adc #6                  ; (C set: +7)
+       adc #$30
        sta cmdbuf,x
        inx
        rts

; the command channel: OPEN 15,dev,15,"cmdbuf" : CLOSE 15 (A = its length)
send_cmd:
        jsr split_off
        ldx #<cmdbuf
        ldy #>cmdbuf
        jsr SETNAM
        lda #15
        ldx FA
        ldy #15
        jsr SETLFS
        jsr OPEN
        lda #15
        jsr CLOSE
        jmp CLRCHN

; the path: /NAME/NAME, in PETSCII. (ptr is preserved by send_cmd: the KERNAL leaves $FB-$FE alone)
path_push:
        ldx pathlen
        cpx #PATHMAX-17
        bcs ++                  ; (very deep: the path shown stops growing, DEL still works)
        lda #$2F
        sta pathbuf,x
        inx
        ldy #E_LEN
        lda (ptr),y
        sta num
        ldy #0
-       cpy num
        beq +
        lda (ptr),y
        sta pathbuf,x
        inx
        iny
        bne -
+       stx pathlen
        rts
++      inc pathover
        rts

; the last name off the path, into lastname, for find_last
path_pop:
        lda pathover
        beq +
        dec pathover
        lda #0
        sta lastlen
        rts
+       ldx pathlen
        stx pathtop
-       dex
        lda pathbuf,x
        cmp #$2F
        bne -
        stx pathlen             ; the / goes
        inx
        ldy #0
-       cpx pathtop
        bcs +
        lda pathbuf,x
        sta lastname,y
        inx
        iny
        cpy #16
        bne -
+       sty lastlen
        rts

; ---------------------------------------------------------------------------
; run or load: A = 1 to run. A clean screen with LOAD"name",8,1 on row 2 and RUN on row 7, and
; RETURNs in the keyboard buffer. From home, READY. prints a line feed, READY. on row 1 and
; another line feed: the cursor is on row 2, on the LOAD. Then: row 3 blank, SEARCHING on 4,
; LOADING on 5, READY. on 6: the cursor on row 7, on the RUN (as seen on the screen dumps).
launch:
        sta l_run
        jsr split_off
        ; the name for the plain way: on PicoD64 "~" (XA picks the entry, however long its name)
        lda fwmode
        beq la_drive
        lda turbo_on
        beq la_plainfw
        lda slow_once
        bne la_plainfw
        lda #$41                ; XA:hhhh first: a wrong checksum falls back to LOAD"#"
        jsr cmd_x_idx
        jsr send_cmd
        lda #$23
        sta t_name
        lda #1
        sta t_namelen
        lda #$4C                ; XL:#hhhh
        sta cmdbuf+1
        lda #$58
        sta cmdbuf
        lda #$3A
        sta cmdbuf+2
        lda #$23
        sta cmdbuf+3
        ldx #4
        jsr put_idx
        jsr cmd_reply
        jsr turbo_answer
        bcs la_plainfw
        jsr save_cfg
        jmp turbo_go
la_plainfw:
        lda #0
        sta slow_once
        lda #$41                ; XA:hhhh
        jsr cmd_x_idx
        jsr send_cmd
        jsr save_cfg
        lda #$23                ; #: the firmware's alias for the entry XA picked
        sta t_name
        lda #1
        sta t_namelen
        jmp la_typed
; a drive's listing: its own name. On a disk mounted on PicoD64, the firmware can still send it
la_drive:
        ldy #E_LEN
        lda (ptr),y
        sta t_namelen
        tay
-       dey
        bmi +
        lda (ptr),y
        sta t_name,y
        jmp -
+       lda fwcap
        beq la_typed
        lda turbo_on
        beq la_typed
        lda slow_once
        bne la_typed
        jsr send_cdup           ; the disk out, the firmware reads the file from its image
        jsr wait_fw
        lda #$58                ; XM:name
        sta cmdbuf
        lda #$4D
        sta cmdbuf+1
        lda #$3A
        sta cmdbuf+2
        ldx #0
-       cpx t_namelen
        beq +
        lda t_name,x
        sta cmdbuf+3,x
        inx
        bne -
+       txa
        clc
        adc #3
        jsr cmd_reply
        jsr turbo_answer
        bcs +
        jsr save_cfg
        jmp turbo_go
+       ; refused or too big: it is back in the drive in a moment
        ldx #0
        ldy #0
-       dex
        bne -
        dey
        bne -
la_typed:
        lda #0
        sta slow_once
        jsr l_screen
        ldx #0
-       cpx t_namelen
        beq +
        lda t_name,x
        jsr CHROUT
        inx
        bne -
+       jmp l_tail

; RUN FIRST: LOAD"*",8,1 and RUN on the disk just inserted
launch_first:
        jsr split_off
        lda #1
        sta l_run
        lda #$2A
        sta t_name
        lda #1
        sta t_namelen
        jmp la_after_name
la_after_name:
        lda fwcap
        beq la_typed
        lda turbo_on
        beq la_typed
        lda rf_noup             ; XI: nothing in the drive to take out
        bne +
        jsr send_cdup
        jsr wait_fw
+       lda #0
        sta rf_noup
        lda #$58
        sta cmdbuf
        lda #$4D
        sta cmdbuf+1
        lda #$3A
        sta cmdbuf+2
        lda #$2A
        sta cmdbuf+3
        lda #4
        jsr cmd_reply
        jsr turbo_answer
        bcs +
        jsr save_cfg
        jmp turbo_go
+       ldx #0                  ; refused: the firmware puts the disk back in the drive in a moment (as la_drive)
        ldy #0
-       dex
        bne -
        dey
        bne -
        jmp la_typed

l_tail:
        lda #$22                ; the closing quote: out of quote mode
        jsr CHROUT
        lda #$2C
        jsr CHROUT
        lda FA                  ; the device, in decimal
        cmp #10
        bcc +
        pha
        lda #$31
        jsr CHROUT
        pla
        sec
        sbc #10
+       ora #$30
        jsr CHROUT
        ldx #0
-       lda t_comma1,x
        beq +
        jsr CHROUT
        inx
        bne -
+       lda #$0D
        sta KEYD
        ldx #1                  ; one RETURN: load only
        lda l_run
        beq l_go
        lda t_namelen           ; "SEARCHING FOR name" takes two rows (or fills one, and a blank follows): READY comes a row lower
        cmp #26
        bcc +
        lda #$0D
        jsr CHROUT
+       ldx #0
-       lda t_run,x
        beq +
        jsr CHROUT
        inx
        bne -
+       lda #$0D
        sta KEYD+1
        ldx #2
l_go:   stx NDX
        lda #$13                ; home
        jsr CHROUT
        jmp READY

l_screen:
        jsr restore_screen
        lda #$93
        jsr CHROUT
        lda #$11
        jsr CHROUT
        lda #$11
        jsr CHROUT
        ldx #0
-       lda t_load,x
        beq +
        jsr CHROUT
        inx
        bne -
+       rts

t_load:   !byte $4C,$4F,$41,$44,$22,0                 ; LOAD"
t_comma1: !byte $2C,$31,0                             ; ,1
t_run:    !byte $0D,$0D,$0D,$0D,$0D                   ; row 7
          !byte $52,$55,$4E,0                         ; RUN

quit:
        jsr restore_screen
        lda #$93
        jsr CHROUT
        jmp READY

restore_screen:
        jsr split_off
        lda #$15                ; the character set a program expects
        sta $D018
        lda #0
        sta $0291
        lda #14
        sta $D020
        sta $0286
        lda #6
        sta $D021
        rts

; ---------------------------------------------------------------------------
; the turbo. A command on channel 15; "00,TURBO,aaaa,llll" means the firmware has it in RAM

; cmdbuf (A = length) on channel 15, the answer in t_reply
cmd_reply:
        jsr split_off
        ldx #<cmdbuf
        ldy #>cmdbuf
        jsr SETNAM
        lda #15
        ldx FA
        ldy #15
        jsr SETLFS
        jsr OPEN
        ldx #0
        stx t_reply
        ldx #15
        jsr CHKIN
        bcs cr_out
        ldy #0
-       sty tt_y
        jsr CHRIN
        ldy tt_y
        sta t_reply,y
        iny
        cmp #$0D
        beq cr_out
        sty tt_y
        pha
        jsr READST
        sta st
        pla
        ldy tt_y
        lda st
        bne cr_out
        cpy #T_REPLY-1
        bcc -
cr_out: lda #15
        jsr CLOSE
        jmp CLRCHN

; is t_reply "00,TURBO,", and does the file fit? C clear if so, with t_addr, t_size, t_end
turbo_answer:
        jsr turbo_parse
        bcs ta_no
        ; it must not touch the turbo's code, or reach the I/O
        lda t_addr+1
        cmp #>(STUB+STUB_LEN)
        bcc ta_cancel
        clc
        lda t_addr
        adc t_size
        sta t_end
        lda t_addr+1
        adc t_size+1
        sta t_end+1
        bcs ta_cancel
        cmp #$D0
        bcc +
        bne ta_cancel
        lda t_end
        bne ta_cancel
+       clc
        rts
ta_cancel:
        lda #$58                ; XC: never mind
        sta cmdbuf
        lda #$43
        sta cmdbuf+1
        lda #2
        jsr send_cmd
ta_no:  sec
        rts

turbo_parse:
        ldy #8
-       lda t_reply,y
        cmp t_want,y
        bne ++
        dey
        bpl -
        ldx #9                  ; the load address and the length
        jsr hex2
        sta t_addr+1
        jsr hex2
        sta t_addr
        inx                     ; the comma
        jsr hex2
        sta t_size+1
        jsr hex2
        sta t_size
        clc
        rts
++      sec
        rts

; XS:t the settings and where we are, for next time (only PicoD64 understands it)
save_cfg:
        lda fwcap
        beq +
        lda #$58
        sta cmdbuf
        lda #$53
        sta cmdbuf+1
        lda #$3A
        sta cmdbuf+2
        lda turbo_on
        ora #$30
        sta cmdbuf+3
        lda #4
        jmp send_cmd
+       rts

; XG: the firmware starts when ATN ends. Channel 15 open with no name: nothing on the bus until
; the two letters; its UNLISTEN is the last thing the firmware hears
send_go:
        lda #0
        ldx #0
        ldy #0
        jsr SETNAM
        lda #15
        ldx FA
        ldy #15
        jsr SETLFS
        jsr OPEN
        ldx #15
        jsr CHKOUT
        lda #$58
        jsr CHROUT
        lda #$47
        jsr CHROUT
        jsr CLRCHN
        lda #0                  ; (no files open now)
        sta $98
        ; what idle lines read as: the constants for rx
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
        rts

copy_stub:
        ldx #STUB_LEN-1
-       lda stub_image,x
        sta STUB,x
        dex
        cpx #$FF
        bne -
        rts

; the file itself: the stub takes over
turbo_go:
        jsr copy_stub
        lda t_addr
        sta $AE
        lda t_addr+1
        sta $AF
        lda t_end
        sta s_endlo
        lda t_end+1
        sta s_endhi
        lda l_run
        sta s_run
        jsr fb_text
        jsr send_go
        jsr restore_screen
        lda #$93
        jsr CHROUT
        jmp STUB

; the fallback's line, where the load can't reach it: two down, LOAD"name",dev,1
S_TEXT = $02A7          ; 89 unused bytes [PRG memory map]
fb_text:
        ldx #0
        ldy #0
-       lda t_fb1,y
        sta S_TEXT,x
        inx
        iny
        cpy #7
        bne -
        ldy #0
-       cpy t_namelen
        beq +
        lda t_name,y
        sta S_TEXT,x
        inx
        iny
        bne -
+       lda #$22
        sta S_TEXT,x
        inx
        lda #$2C
        sta S_TEXT,x
        inx
        lda FA
        cmp #10
        bcc +
        lda #$31
        sta S_TEXT,x
        inx
        lda FA
        sec
        sbc #10
+       ora #$30
        sta S_TEXT,x
        inx
        lda #$2C
        sta S_TEXT,x
        inx
        lda #$31
        sta S_TEXT,x
        inx
        lda l_run               ; to run: RUN on row 7, as launch does
        beq fb_end
        ldy #0
-       lda t_run,y
        beq fb_end
        sta S_TEXT,x
        inx
        iny
        bne -
fb_end: lda #0
        sta S_TEXT,x
        rts
t_fb1:  !byte $11,$11,$4C,$4F,$41,$44,$22

; two hex digits at t_reply,x: their value in A
hex2:   jsr hex1
        asl
        asl
        asl
        asl
        sta tt_y
        jsr hex1
        ora tt_y
        rts
hex1:   lda t_reply,x
        inx
        cmp #$41
        bcc +
        sbc #7                  ; (C set)
+       and #$0F
        rts

t_want: !byte $30,$30,$2C,$54,$55,$52,$42,$4F,$2C     ; 00,TURBO,
t_cfg:  !byte $30,$30,$2C,$43,$46,$47,$2C             ; 00,CFG,

; the part that survives the load: in the cassette buffer, $033C-$03FB [PRG memory map]
STUB = $033C
stub_image:
!pseudopc STUB {
stub_start:
        sei
        lda $D011               ; the screen off: no bad lines stealing the cycles
        and #$EF
        sta $D011
        ldx #0                  ; a frame or two, for that to take
        ldy #40
-       dex
        bne -
        dey
        bne -
s_loop:
        lda $AE
s_endlo = * + 1
        cmp #0
        bne +
        lda $AF
s_endhi = * + 1
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
s_sum:  jsr rx                  ; the checksum
        cmp s_x
        php
        lda $D011
        ora #$10
        sta $D011
        plp                     ; (first: it would put the I flag back)
        cli
        bne s_bad
        lda $AE                 ; as LOAD leaves it
        sta $2D
        lda $AF
        sta $2E
        jsr $A533               ; the BASIC links, as a direct LOAD does
        lda s_run
        beq s_rdy
        ldx #3
-       lda s_runk,x
        sta KEYD,x
        dex
        bpl -
        lda #4
        sta NDX
s_rdy:  jmp READY

; a wrong checksum: a plain LOAD, typed: the line try_turbo prepared
s_bad:  ldx #0
-       lda S_TEXT,x
        beq +
        jsr CHROUT
        inx
        bne -
+       lda #$0D
        sta KEYD
        sta KEYD+1              ; (read only if s_run makes the count 2)
        ldx s_run
        inx
        stx NDX
        lda #$13
        jsr CHROUT
        jmp READY

; one byte. Reads at c0+16, 24, 32, 40 cycles, c0 being the write that pulls DATA
rx:
rx_pull:
        lda #0                  ; 2
        sta $DD00               ; 4: c0
rx_rel:
        lda #0                  ; 2
        sta $DD00               ; 4: c0+6, released
        nop                     ; c0+8
        nop                     ; c0+10
        nop                     ; c0+12
        lda $DD00               ; its read: c0+16
        lsr
        lsr
        eor $DD00               ; c0+24
        lsr
        lsr
        eor $DD00               ; c0+32
        lsr
        lsr
        eor $DD00               ; c0+40
rx_const:
        eor #0
        rts

s_x:    !byte 0
s_run:  !byte 0
s_runk: !byte $52,$55,$4E,$0D           ; RUN
stub_end:
}
STUB_LEN = stub_end - stub_start
!if STUB_LEN > 192 { !error "the turbo's code no longer fits the cassette buffer: ", STUB_LEN }

; ---------------------------------------------------------------------------
; options for the selected item: only what applies
do_menu:
        jsr selected
        bcc +
        jmp wait
+       cmp #T_PRG
        beq +
        cmp #T_DISK
        beq ++
        jmp wait                ; nothing to offer
+       lda #<menu_prg
        ldx #>menu_prg
        ldy #3
        bne m_set
++      lda #<menu_disk
        ldx #>menu_disk
        ldy #3
m_set:  sta m_items
        stx m_items+1
        sty m_n
        lda #0
        sta m_cur
m_loop:
        jsr m_draw
-       jsr GETIN
        bne +
        jsr joy
        beq -
+       cmp #K_DOWN
        bne +
        ldx m_cur
        inx
        cpx m_n
        bcs m_loop
        stx m_cur
        bcc m_loop
+       cmp #K_UP
        bne +
        lda m_cur
        beq m_loop
        dec m_cur
        jmp m_loop
+       cmp #K_RET
        beq m_go
        cmp #K_F1
        beq m_out
        cmp #K_DEL
        beq m_out
        cmp #K_STOP
        beq m_out
        jmp m_loop
m_out:  jmp loop

m_go:   jsr selected
        cmp #T_DISK
        beq m_disk
        lda m_cur               ; a program: 0 run, 1 load only, 2 run without the turbo
        bne +
        lda #1
        jmp launch
+       cmp #1
        bne +
        lda #0
        jmp launch
+       lda #1
        sta slow_once
        jmp launch
m_disk: lda m_cur               ; a disk: 0 open, 1 run its first program, 2 insert and go to BASIC
        bne +
        jmp go_in
+       cmp #1
        bne +
        ; RUN FIRST. With PicoD64's turbo: XI:hhhh names the D64 that XM:* reads, nothing mounted; the firmware puts
        ; it in the drive when the transfer ends. (A mount then CD_ at once raced on the board: the drive, just
        ; online, could miss the CD_, and the load went the plain way, 2026-11-07.) Otherwise: CD into it, as before
        lda fwmode
        beq rf_cd
        lda fwcap
        beq rf_cd
        lda turbo_on
        beq rf_cd
        lda #$49                ; XI:hhhh
        jsr cmd_x_idx
        jsr send_cmd
        lda #1
        sta rf_noup
        bne rf_go
rf_cd:  jsr cd_selected
rf_go:  lda #0
        sta fwmode
        jmp launch_first
+       jsr cd_selected
        jmp quit

; the box: framed, at column M_COL from row M_ROW, over whatever the list has there. The cursor's
; row looks just like the list's cursor: white and reversed. m_items -> 12-character items
M_COL = 25
M_W   = 14
M_ROW = 7
m_draw:
        lda m_items
        sta vp
        lda m_items+1
        sta vp+1
        ldx #M_ROW
        lda #$70                ; top left
        ldy #$6E                ; top right
        jsr m_edge
        lda #0
        sta m_i
m_row:
        lda m_i
        clc
        adc #M_ROW+1
        tax
        jsr m_at
        lda #$5D                ; the sides
        ldy #M_COL
        sta (num),y
        ldy #M_COL+M_W-1
        sta (num),y
        lda #0
        sta m_x
-       ldy m_x
        lda (vp),y
        jsr uc_fix
        ldx m_i
        cpx m_cur
        bne +
        ora #$80
+       pha
        lda m_x
        clc
        adc #M_COL+1
        tay
        pla
        sta (num),y
        inc m_x
        lda m_x
        cmp #M_W-2
        bne -
        lda #C_TEXT
        ldx m_i
        cpx m_cur
        bne +
        lda #C_SEL
+       jsr m_paint
        lda vp
        clc
        adc #M_W-2
        sta vp
        bcc +
        inc vp+1
+       inc m_i
        lda m_i
        cmp m_n
        bne m_row
        clc
        adc #M_ROW+1
        tax
        lda #$6D                ; bottom left
        ldy #$7D                ; bottom right
        ; falls into m_edge

; a rule across the box: X the row, A the left corner, Y the right
m_edge:
        sty m_r
        pha
        jsr m_at
        pla
        ldy #M_COL
        sta (num),y
        ldy #M_COL+M_W-1
        lda m_r
        sta (num),y
        lda #$40
        ldy #M_COL+1
-       sta (num),y
        iny
        cpy #M_COL+M_W-1
        bne -
        lda #C_TEXT
        ; falls into m_paint

; the box's width of the row at num in colour A
m_paint:
        pha
        lda num+1
        clc
        adc #>(COLOUR-SCREEN)
        sta num+1
        pla
        ldy #M_COL
-       sta (num),y
        iny
        cpy #M_COL+M_W
        bne -
        lda num+1
        sec
        sbc #>(COLOUR-SCREEN)
        sta num+1
        rts

; num -> row X of the screen
m_at:
        lda scrlo,x
        sta num
        lda scrhi,x
        sta num+1
        rts

menu_prg:
        !scr " Run        "
        !scr " Load       "
        !scr " Run slow   "
menu_disk:
        !scr " Open       "
        !scr " Run first  "
        !scr " Insert     "

; ---------------------------------------------------------------------------
; XR: C clear if PicoD64 answered, which sets fwcap and the turbo setting
try_xr:
        lda #$58
        sta cmdbuf
        lda #$52
        sta cmdbuf+1
        lda #2
        jsr cmd_reply
        ldy #6
-       lda t_reply,y
        cmp t_cfg,y
        bne +
        dey
        bpl -
        lda #1
        sta fwcap
        lda t_reply+7
        and #1
        sta turbo_on
        clc
        rts
+       sec
        rts

; the directory: PicoD64's own listing if it gives one, else OPEN 2,dev,0,"$"
read_dir:
        jsr split_off
        lda #0
        sta ucase
        sta hostfs
        sta fwmode
        sta big
        sta cut
        sta w_start
        sta w_start+1
        sta sel
        sta sel+1
        lda fwcap
        beq +
        jsr fetch_window
        bcs +
        rts
+
        lda #1                  ; entry 0: back
        sta count
        lda #0
        sta count+1
        sta disklen
        sta err
        jsr back_entry
        lda #1
        sta first_line
        lda #1
        ldx #<t_dollar
        ldy #>t_dollar
        jsr SETNAM
        lda #2
        ldx FA
        ldy #0
        jsr SETLFS
        jsr OPEN
        bcs rd_err
        ldx #2
        jsr CHKIN
        bcs rd_err
        jsr rbyte               ; the load address
        jsr rbyte
        lda st
        bne rd_err
rd_line:
        jsr rbyte               ; the link
        sta num
        jsr rbyte
        ora num
        beq rd_out
        lda st
        bne rd_out
        jsr rbyte               ; the line number: the blocks
        sta l_blk
        jsr rbyte
        sta l_blk+1
        lda st
        bne rd_out
        jsr rd_text
        lda st
        beq rd_line
rd_out:
        lda #2
        jsr CLOSE
        jsr CLRCHN
        lda hostfs              ; a disk's listing (not a PC folder's): as a C64 lists it
        bne +
        lda #$80
        sta ucase
+       rts
rd_err:
        lda #1
        sta err
        jmp rd_out

rbyte:
        jsr CHRIN
        pha
        jsr READST
        sta st
        pla
        rts

t_dollar: !byte $24

; a line's text, up to its 0: a name in quotes, then its type
rd_text:
        ldx count
        lda count+1
        jsr entry_ptr
        lda #0
        sta l_state
        sta l_len
        sta l_type
rd_ch:
        jsr rbyte
        tax
        bne +
        jmp rd_end
+       lda st
        beq +
        jmp rd_end
+       txa
        ldx l_state
        beq rd_s0
        dex
        beq rd_s1
        dex
        beq rd_s2
        dex
        bne *+5
        jmp rd_s3
        jmp rd_ch
rd_s0:                          ; to the opening quote
        cmp #$22
        bne rd_ch
        inc l_state
        jmp rd_ch
rd_s1:                          ; the name
        cmp #$22
        bne +
        inc l_state
        jmp rd_ch
+       ldy l_len
        ldx first_line
        beq +
        cpy #16                 ; the header
        bcs rd_ch
        sta diskname,y
        jmp ++
+       cpy #46                 ; a disk's are 16; a PC folder as a drive (VICE) has longer
        bcc rd_keep
        ; too long: start*.ext, so it still loads, and the extension still says what it is
        pha
        ldy #43
        lda (ptr),y
        ldy #42
        sta (ptr),y
        ldy #44
        lda (ptr),y
        ldy #43
        sta (ptr),y
        ldy #45
        lda (ptr),y
        ldy #44
        sta (ptr),y
        pla
        ldy #45
        sta (ptr),y
        lda #$2A
        ldy #41
        sta (ptr),y
        jmp rd_ch
rd_keep:
        sta (ptr),y
++      inc l_len
        jmp rd_ch
rd_s2:                          ; spaces (and a splat) before the type
        cmp #$20
        bne *+5
        jmp rd_ch
        cmp #$2A
        bne *+5
        jmp rd_ch
        ldy first_line
        beq *+5
        jmp rd_ch
        inc l_state
        ldy #E_TTXT
        sta (ptr),y
        ldx #T_OTHER
        cmp #$50                ; P: PRG
        bne +
        ldx #T_PRG
+       cmp #$44                ; D: DIR
        bne +
        ldx #T_DIR
+       txa
        ldy #E_TYPE
        sta (ptr),y
        jmp rd_ch
rd_s3:                          ; the type's other two letters
        ldy l_type
        bne rd_s3b
        cmp #$45                ; DE: DEL, a disk's art, not DIR
        bne rd_s3b
        pha
        ldy #E_TYPE
        lda (ptr),y
        cmp #T_DIR
        bne +
        lda #T_OTHER
        sta (ptr),y
+       pla
        ldy l_type
rd_s3b:
        cpy #2
        bcs +
        inc l_type
        pha
        tya
        clc
        adc #E_TTXT+1
        tay
        pla
        sta (ptr),y
+       jmp rd_ch

rd_end:
        lda first_line
        beq +
        lda l_len               ; the header: the disk's name
        sta disklen
        lda #0
        sta first_line
        rts
+       lda l_state             ; no name: BLOCKS FREE
        cmp #2
        bcc rd_skip
        jsr is_dots
        bcs rd_skip
        jsr is_me               ; the firmware lists this program at the root: not in our own list
        bcs rd_skip
        lda l_len
        ldy #E_LEN
        sta (ptr),y
        ldy #E_DLEN
        sta (ptr),y
        lda l_blk
        ldy #E_BLK
        sta (ptr),y
        lda l_blk+1
        iny
        sta (ptr),y
        jsr classify
        lda count               ; keep it, if there is room
        cmp #<MAXENT
        lda count+1
        sbc #>MAXENT
        bcs rd_skip
        inc count
        bne rd_skip
        inc count+1
rd_skip:
        rts

; is it . or .., a folder on a PC being a drive (VICE)? Only such a drive lists them, so it also says
; that the names are PC names: from here on the extension is the format. C set if so
is_dots:
        ldy #E_TYPE
        lda (ptr),y
        cmp #T_DIR
        bne id_no
        lda l_len
        cmp #3
        bcs id_no
        ldy #0
        lda (ptr),y
        cmp #$2E
        bne id_no
        lda #1
        sta hostfs
        sec
        rts
id_no:  clc
        rts

; is the entry at ptr BROWSER, first on the root's listing? C set if so
is_me:
        lda pathlen
        bne im_no
        lda count+1
        bne im_no
        lda count
        cmp #1
        bne im_no
        lda l_len
        cmp #7
        bne im_no
        ldy #6
-       lda (ptr),y
        cmp t_me,y
        bne im_no
        dey
        bpl -
        sec
        rts
im_no:  clc
        rts
t_me:   !byte $42,$52,$4F,$57,$53,$45,$52       ; BROWSER

; the extension: NAME.D64 is a disk, shown as D64; a .PRG, or no extension, a program; anything else
; on PicoD64 is a format that can't run from here, shown as itself (CRT, T64, TXT...)
classify:
        ldy #E_TYPE
        lda (ptr),y
        cmp #T_DIR
        beq cl_out
        cmp #T_OTHER
        bne +
        lda fwmode
        beq cl_out
+       ldy #E_LEN
        lda (ptr),y
        sta num
        ; the last dot, at most 4 from the end
        tay
        ldx #4
-       dey
        bmi cl_none
        lda (ptr),y
        cmp #$2E
        beq cl_dot
        dex
        bne -
cl_none:
        lda fwmode
        beq cl_out
        lda #T_PRG              ; no extension on a card: the usual C64 program
        ldy #E_TYPE
        sta (ptr),y
        ldx #2
-       lda t_prg,x
        ldy #E_TTXT
        sty sptr+0
        jsr cl_put
        dex
        bpl -
cl_out: rts
cl_dot: cpy #0
        beq cl_none             ; (a name that is only .xyz)
        sty cl_len
        ; the three letters, in capitals, spaces after a short one
        iny
        ldx #0
-       cpy num
        bcs +
        lda (ptr),y
        jsr fold_up
        sta cl_a,x
        iny
        inx
        cpx #3
        bne -
        beq ++
+       lda #$20
        sta cl_a,x
        inx
        cpx #3
        bne -
++      ldx #2
-       lda cl_a,x
        jsr cl_put
        dex
        bpl -
        lda cl_len              ; the name as shown has no extension
        ldy #E_DLEN
        sta (ptr),y
        ; which?
        lda cl_a
        cmp #$44
        bne cl_p
        lda cl_b
        cmp #$36
        bne cl_x
        lda cl_c
        cmp #$34
        bne cl_x
        lda #T_DISK
        bne cl_set
cl_p:   cmp #$50
        bne cl_x
        lda cl_b
        cmp #$52
        bne cl_x
        lda cl_c
        cmp #$47
        bne cl_x
        lda #T_PRG
        beq cl_set              ; (T_PRG is 0)
cl_x:   lda hostfs
        bne cl_o
        lda fwmode              ; on a drive a PRG named x.y is still a program
        beq cl_keep
cl_o:   lda #T_OTHER
cl_set: ldy #E_TYPE
        sta (ptr),y
        rts
cl_keep:
        ldy #E_DLEN             ; ... shown whole
        lda num
        sta (ptr),y
        ldx #2                  ; ... and a PRG
-       lda t_prg,x
        jsr cl_put
        dex
        bpl -
        rts
; A into the entry's format, letter X
cl_put: pha
        txa
        clc
        adc #E_TTXT
        tay
        pla
        sta (ptr),y
        rts
t_prg:  !byte $50,$52,$47

; ---------------------------------------------------------------------------
; PicoD64's listing, a window of it, over the turbo: w_want (FFFF: round the last entry), the filter
fetch_window:
        lda #$58                ; XD:hhhh[,filter]
        sta cmdbuf
        lda #$44
        sta cmdbuf+1
        lda #$3A
        sta cmdbuf+2
        ldx #3
        lda w_want+1
        jsr put_hex
        lda w_want
        jsr put_hex
        lda flen
        beq ++
        lda #$2C
        sta cmdbuf,x
        inx
        ldy #0
-       lda fbuf,y
        sta cmdbuf,x
        inx
        iny
        cpy flen
        bne -
++      txa
        jsr cmd_reply
        jsr turbo_parse
        bcc +
        rts                     ; C set: no such command, this isn't PicoD64's card side
+       jsr send_go
        sei
        lda $D011               ; the screen off: no bad lines stealing the cycles
        and #$EF
        sta $D011
        ldx #0
        ldy #20
-       dex
        bne -
        dey
        bne -
        lda #0
        sta s_x
        jsr rxx
        sta total
        jsr rxx
        sta total+1
        jsr rxx
        sta w_start
        jsr rxx
        sta w_start+1
        jsr rxx
        sta w_n
        jsr rxx
        sta w_n+1
        jsr rxx
        sta cut
        jsr rxx
        sta sel
        jsr rxx
        sta sel+1
        jsr rxx                 ; the folder: /GAMES/ARCADE, or nothing at the root
        sta num
        ldy #0
-       cpy num
        beq +
        jsr rxx
        sta pathbuf,y
        iny
        bne -
+       sty pathlen
        cpy #1
        bne +
        lda pathbuf
        cmp #$2F
        bne +
        lda #0
        sta pathlen
+       lda #1
        sta fwmode
        sta count
        lda #0
        sta count+1
        sta pathover
        jsr back_entry
        lda #0
        sta f_val               ; entries read
        sta f_val+1
fw_entry:
        lda f_val
        cmp w_n
        bne +
        lda f_val+1
        cmp w_n+1
        beq fw_end
+       ldx count
        lda count+1
        jsr entry_ptr
        jsr rxx
        beq +
        lda #T_DIR
        !byte $2C               ; (skip the next 2 bytes)
+       lda #T_OTHER
        ldy #E_TYPE
        sta (ptr),y
        jsr rxx
        ldy #E_BLK
        sta (ptr),y
        jsr rxx
        ldy #E_BLK+1
        sta (ptr),y
        jsr rxx
        ldy #E_LEN
        sta (ptr),y
        ldy #E_DLEN
        sta (ptr),y
        sta num
        ldy #0
-       cpy num
        beq +
        jsr rxx
        sta (ptr),y
        iny
        bne -
+       clc                     ; its number in the firmware's list
        lda w_start
        adc f_val
        ldy #E_IDX
        sta (ptr),y
        lda w_start+1
        adc f_val+1
        iny
        sta (ptr),y
        ldy #E_TTXT             ; blank unless classify finds one
        lda #$20
        sta (ptr),y
        iny
        sta (ptr),y
        iny
        sta (ptr),y
        jsr classify
        inc count
        bne +
        inc count+1
+       inc f_val
        bne fw_entry
        inc f_val+1
        jmp fw_entry
fw_end: jsr rx                  ; the checksum
        cmp s_x
        php
        lda $D011
        ora #$10
        sta $D011
        plp                     ; (first: it would put the I flag back)
        cli
        beq +
        lda #1                  ; a wrong byte: say so, the list may be wrong
        sta err
+       ; is there more than this window?
        sec
        lda count
        sbc #1
        cmp total
        bne ++
        lda count+1
        sbc #0
        cmp total+1
        bne ++
        lda cut
        bne ++
        lda flen                ; a filtered list came from the firmware: keep asking it
        bne +++
        lda #0
        sta big
        clc
        rts
++      lda #1
        sta big
+++     clc
        rts

rxx:    jsr rx
        pha
        eor s_x
        sta s_x
        pla
        rts

; entry 0: back
back_entry:
        ldx #0
        txa
        jsr entry_ptr
        ldy #E_NAME
        lda #$42
        sta (ptr),y
        iny
        lda #$61
        sta (ptr),y
        iny
        lda #$63
        sta (ptr),y
        iny
        lda #$6B
        sta (ptr),y
        ldy #E_LEN
        lda #4
        sta (ptr),y
        ldy #E_DLEN
        sta (ptr),y
        ldy #E_TYPE
        lda #T_UP
        sta (ptr),y
        ldy #E_TTXT
        lda #$20
        sta (ptr),y
        iny
        sta (ptr),y
        iny
        sta (ptr),y
        rts

; the cursor on the entry the firmware named: the one we came back from
cur_from_sel:
        lda fwmode
        beq +
        lda sel
        ora sel+1
        beq +
        sec
        lda sel
        sbc w_start
        sta cur
        lda sel+1
        sbc w_start+1
        sta cur+1
        lda pathlen             ; the back row
        beq +
        inc cur
        bne +
        inc cur+1
+       rts

; a typed letter, on a folder too big for RAM: the firmware searches the whole folder
server_filter:
        lda NDX                 ; more keys waiting: only fetch when typing pauses
        beq +
        rts
+       lda #0
        sta w_want
        sta w_want+1
        jsr fetch_window
        lda #1
        sta big
        jmp rebuild

; the cursor at either end of a window of a big folder: the next one
; the firmware sends 100 entries (BR_XWIN). Going down: a window with a screenful above the cursor;
; going up: with a screenful below it
WC_BEFORE = 20
WC_AFTER  = 80
window_check:
        lda big
        bne *+5
        jmp wc_out
        lda fwmode
        bne *+5
        jmp wc_out
        ; at the last row, with more after it?
        clc
        lda cur
        adc #1
        sta num
        lda cur+1
        adc #0
        cmp nvis+1
        bne wc_up
        lda num
        cmp nvis
        bne wc_up
        clc
        lda w_start
        adc w_n
        sta num
        lda w_start+1
        adc w_n+1
        sta num+1
        lda num
        cmp total
        bne +
        lda num+1
        cmp total+1
        beq wc_up
+       ; the absolute row, and a window that starts a screenful before it
        jsr wc_abs
        sec
        lda wc_a
        sbc #WC_BEFORE
        sta w_want
        lda wc_a+1
        sbc #0
        sta w_want+1
        bcs wc_get
        lda #0
        sta w_want
        sta w_want+1
        jmp wc_get
wc_up:  lda w_start
        ora w_start+1
        beq wc_out
        lda cur+1
        bne wc_out
        lda cur
        cmp #2
        bcs wc_out
        jsr wc_abs
        sec
        lda wc_a
        sbc #WC_AFTER
        sta w_want
        lda wc_a+1
        sbc #0
        sta w_want+1
        bcs wc_get
        lda #0
        sta w_want
        sta w_want+1
wc_get: jsr fetch_window
        jsr rebuild
        sec                     ; the same entry, in the new window
        lda wc_a
        sbc w_start
        sta cur
        lda wc_a+1
        sbc w_start+1
        sta cur+1
        lda flen
        bne wc_out
        lda pathlen
        beq wc_out
        inc cur
        bne wc_out
        inc cur+1
wc_out: rts
; wc_a = the firmware's number of the cursor's row
wc_abs: clc
        lda w_start
        adc cur
        sta wc_a
        lda w_start+1
        adc cur+1
        sta wc_a+1
        lda flen
        bne +
        lda pathlen
        beq +
        lda wc_a
        bne ++
        dec wc_a+1
++      dec wc_a
+       rts

; ---------------------------------------------------------------------------
; the parts of the screen that don't change
frame:
        ldx #0
        lda #$20
-       sta SCREEN,x
        sta SCREEN+$100,x
        sta SCREEN+$200,x
        sta SCREEN+$300-8,x
        inx
        bne -
        lda #C_TEXT
        ldx #0
-       sta COLOUR,x
        sta COLOUR+$100,x
        sta COLOUR+$200,x
        sta COLOUR+$300-8,x
        inx
        bne -
        ldx #39
-       lda #$40                ; the rules: a line across
        sta SCREEN+40*ROW_RULE1,x
        sta SCREEN+40*ROW_RULE2,x
        lda #C_DIM
        sta COLOUR+40*ROW_RULE1,x
        sta COLOUR+40*ROW_RULE2,x
        dex
        bpl -
        ldx #0
-       lda t_title,x
        beq +
        sta SCREEN+40*ROW_TITLE+1,x
        lda #C_HEAD
        sta COLOUR+40*ROW_TITLE+1,x
        inx
        bne -
+       rts

t_title: !scr "PicoD64",0

; a character of a name to a screen code, for the upper and lower case set.
; PicoD64's listing is ASCII, as on the card; a drive's is PETSCII
screen_code:
        ldx fwmode
        bne sc_ascii
sc_pet: pha                     ; a PC's folder (VICE): small letters and _ as they are on the PC
        lda hostfs
        beq sc_p1
        pla
        cmp #$5F
        bne +
        lda #$64
        rts
+       cmp #$41
        bcc sc_p2
        cmp #$5B
        bcs sc_p2
        sbc #$3F                ; (C clear) $41-$5A: - $40
        rts
sc_p1:  pla
sc_p2:  cmp #$E0                ; the graphics that a disk's art uses, so it looks like art
        bcc +
        sbc #$80                ; (C set) $E0-$FF: $60-$7F
        rts
+       cmp #$C0
        bcc +
        and #$7F                ; $C0-$DF: the line, shifted letters as capitals, more graphics
        rts
+       cmp #$A0
        bcc +
        sbc #$40                ; (C set) $A0-$BF: $60-$7F
        rts
+       cmp #$61
        bcc +
        cmp #$7B
        bcs sc_q
        sec
        sbc #$60                ; (our own Back row)
        rts
+       cmp #$5F
        bne +
        lda #$1F                ; the left arrow
        rts
+       cmp #$40
        bne +
        lda #0
        rts
+       cmp #$20
        bcc sc_q
        cmp #$5B
        bcc sc_out              ; $20-$5A: the same
        cmp #$5F
        bcs sc_q
        and #$3F                ; $5B-$5E: [ pound ] up arrow, as in "[cr Triad]"
        rts
sc_q:   lda #$3F
sc_out: rts
sc_ascii:
        cmp #$7B
        bcc sc_a1
        cmp #$7E                ; ~ marks where a long name was cut: no tilde in the character set
        bne sc_br
        lda #$2D
        rts
sc_br:  cmp #$7D                ; { and } as ( and )
        bne sc_br2
        lda #$29
        rts
sc_br2: cmp #$7B
        bne sc_q
        lda #$28
        rts
sc_a1:  cmp #$61
        bcc +
        sec
        sbc #$60                ; a-z
        rts
+       cmp #$5F
        bne +
        lda #$64                ; a real underscore
        rts
+       cmp #$40
        bne +
        lda #0
        rts
+       cmp #$20
        bcc sc_q
        cmp #$5B
        bcc sc_out              ; digits, punctuation, A-Z
        cmp #$5B
        bne +
        lda #$1B
        rts
+       cmp #$5D
        bne sc_q
        lda #$1D
        rts

;------------------------------------------------------------------------------
; A drive's listing as a C64 lists it: its names in the uppercase/graphics set, where shifted letters are
; graphics, so directory art (a logo drawn in the names) looks as it does after LOAD"$",8 and LIST. The rest
; of the screen stays in the upper and lower case set: a raster split switches the set at the two rules,
; whose line is the same in both sets, so the change can land anywhere on them.

; a name's character to a screen code, in the list's rows
list_code:
        bit ucase
        bmi sc_up
        jmp screen_code

; PETSCII to the uppercase set, as LIST prints it inside quotes: control codes as reversed characters
sc_up:  cmp #$20
        bcs su_1
        cmp #$0D                ; RETURN ends a line, even inside quotes
        beq su_q
        ora #$80                ; $00-$1F: reversed @, A-Z...
        rts
su_1:   cmp #$40
        bcc su_out              ; $20-$3F: the same
        cmp #$60
        bcs +
        and #$1F                ; $40-$5F: $00-$1F
        rts
+       cmp #$80
        bcs +
        and #$5F                ; $60-$7F: $40-$5F
        rts
+       cmp #$A0
        bcs +
        cmp #$8D                ; shifted RETURN, the same
        beq su_q
        eor #$40                ; $80-$9F: reversed $40-$5F
        rts
+       cmp #$C0
        bcs +
        eor #$C0                ; $A0-$BF: $60-$7F
        rts
+       cmp #$FF
        beq +
        and #$7F                ; $C0-$FE: $40-$7E
        rts
+       lda #$5E                ; $FF: pi
        rts
su_q:   lda #$3F
su_out: rts

SPLIT_TOP = 51 + 8 * ROW_RULE1          ; the rules' first raster lines
SPLIT_BOT = 51 + 8 * ROW_RULE2

; the split on, if the list is a drive's (and it isn't on)
split_on:
        bit ucase
        bpl +
        lda split
        bne +
        sei
        lda #<split_irq
        sta $0314
        lda #>split_irq
        sta $0315
        lda #0
        sta splitst
        lda #SPLIT_TOP
        sta $D012
        lda $D011
        and #$7F
        sta $D011
        lda #$0F                ; (any old raster flag)
        sta $D019
        lda #1
        sta $D01A
        sta split
        cli
+       rts

; the split off (every register kept), and the list's rows blank (in the other set they would look wrong), before any I/O or when
; leaving: the KERNAL's serial routines hold interrupts off, and the split would land late
split_off:
        pha
        lda split
        bne +
        pla
        rts
+       txa
        pha
        tya
        pha
        lda sptr
        pha
        lda sptr+1
        pha
        sei
        lda #0
        sta $D01A
        sta split
        lda #$0F
        sta $D019
        lda #<$EA31
        sta $0314
        lda #>$EA31
        sta $0315
        lda #$17
        sta $D018
        cli
        ldx #LIST_ROW
-       lda scrlo,x
        sta sptr
        lda scrhi,x
        sta sptr+1
        ldy #39
        lda #$20
--      sta (sptr),y
        dey
        bpl --
        inx
        cpx #LIST_ROW+LIST_H
        bne -
        pla
        sta sptr+1
        pla
        sta sptr
        pla
        tay
        pla
        tax
        pla
        rts

; the KERNAL's IRQ vector while the split is on. The timer's interrupt (keys, clock) runs with interrupts
; on again, so the split never waits for it
split_irq:
        lda $D019
        and #1
        beq si_cia
        sta $D019
        lda splitst
        bne +
        lda #$15                ; the top rule: the list's set
        sta $D018
        lda #SPLIT_BOT
        sta $D012
        inc splitst
        jmp $EA81
+       lda #$17                ; the bottom rule: back for the footer and the top of the next frame
        sta $D018
        lda #SPLIT_TOP
        sta $D012
        dec splitst
        jmp $EA81
si_cia: lda $DC0D
        cli
        jmp $EA31

; ---------------------------------------------------------------------------
; everything that changes
draw:
        jsr split_on
        jsr draw_turbo
        jsr draw_list
        jsr draw_path
        jsr draw_foot
        jmp draw_filter

draw_turbo:
        ldx #8
-       lda turbo_on
        beq +
        lda t_ton,x
        bne ++
+       lda t_toff,x
++      sta SCREEN+40*ROW_TITLE+30,x
        lda #C_TEXT
        sta COLOUR+40*ROW_TITLE+30,x
        dex
        bpl -
        rts
t_ton:  !scr " Turbo on"
t_toff: !scr "Turbo off"

draw_list:
        lda top
        sta d_i
        lda top+1
        sta d_i+1
        lda #0
        sta d_row
d_row_loop:
        lda d_row
        clc
        adc #LIST_ROW
        tax
        lda scrlo,x
        sta sptr
        sta num
        lda scrhi,x
        sta sptr+1
        clc
        adc #>(COLOUR-SCREEN)
        sta num+1               ; num: this row's colours
        ldy #39
        lda #$20
-       sta rowbuf,y
        dey
        bpl -
        lda #C_TEXT
        sta d_col
        lda d_i                 ; past the end: a blank row
        cmp nvis
        lda d_i+1
        sbc nvis+1
        bcc +
        jmp d_put
+       ldx d_i
        lda d_i+1
        jsr vis_entry
        jsr draw_name
        ldy #E_TYPE
        lda (ptr),y
        tax
        lda icon,x
        sta rowbuf+1
        lda typecol,x
        sta d_col
        cpx #T_UP
        bne +
        lda #$42                ; "Back": its capital, whichever way names are shown
        sta rowbuf+3
        bit ucase
        bpl d_put
        ldy #3                  ; in the uppercase set: BACK
-       lda t_backu,y
        sta rowbuf+3,y
        dey
        bpl -
        jmp d_put
+
        cpx #T_DIR
        beq d_put
        ldy #E_TTXT             ; the format, so a D64 is told from a CRT: columns 32-34
        lda (ptr),y
        jsr fmt_char
        sta rowbuf+32
        iny
        lda (ptr),y
        jsr fmt_char
        sta rowbuf+33
        iny
        lda (ptr),y
        jsr fmt_char
        sta rowbuf+34
        cpx #T_DISK
        bne +
        jsr d_tag               ; a disk's size says nothing: which disk it is, if the name says
        jmp d_put
+
        ldy #E_BLK+1            ; nor a file of 5 digits, not a C64's: it would touch the format
        lda (ptr),y
        cmp #$27
        bcs d_put
        jsr d_size
d_put:
        lda d_i                 ; the cursor's row is reversed
        cmp cur
        bne d_norm
        lda d_i+1
        cmp cur+1
        bne d_norm
        ldy #39
-       lda rowbuf,y
        ora #$80
        sta (sptr),y
        lda #C_SEL
        sta (num),y
        dey
        bpl -
        jmp d_next
d_norm:
        ldy #39
-       lda rowbuf,y
        sta (sptr),y
        lda d_col
        sta (num),y
        dey
        bpl -
d_next:
        inc d_i
        bne +
        inc d_i+1
+       inc d_row
        lda d_row
        cmp #LIST_H
        beq +
        jmp d_row_loop
+       rts

t_backu: !byte $02,$01,$03,$0B
icon:    !byte $2D, $3E, $2E, $1F, $2A      ; - program, > folder, . other, back arrow, * disk
typecol: !byte C_PRG, C_DIR, 12, C_TEXT, C_DISK

; the blocks, right-aligned to column 38
; which disk: "Disk 1 of 2 Side A" anywhere in the name makes 1A; else the last word, if it is 4
; characters or fewer and has a digit, or is one letter: _0, _1a, v1.1. Long names are cut in the
; middle, and this is what tells a set's disks apart
d_tag:
        ldy #E_DLEN
        lda (ptr),y
        sta dt_len
        lda #0
        sta dt_o
        sta dt_i
ds_scan:
        ldy dt_i
        iny
        iny
        iny
        iny
        cpy dt_len
        bcc +
        jmp ds_done
+       ldy dt_i                ; not the end of a longer word
        beq ++
        dey
        lda (ptr),y
        jsr ds_alnum
        bcc ++
ds_next:
        inc dt_i
        jmp ds_scan
++      ldy dt_i
        lda (ptr),y
        jsr fold_up
        sta dt_w
        iny
        lda (ptr),y
        jsr fold_up
        cmp #$49                ; I
        bne ds_next
        iny
        lda (ptr),y
        jsr fold_up
        sta dt_w+1
        iny
        lda (ptr),y
        jsr fold_up
        ldx dt_w
        cpx #$44                ; D I S K/C
        bne +
        ldx dt_w+1
        cpx #$53
        bne ds_next
        cmp #$4B
        beq ds_disk
        cmp #$43
        beq ds_disk
        bne ds_next
+       cpx #$53                ; S I D E
        bne ds_next
        ldx dt_w+1
        cpx #$44
        bne ds_next
        cmp #$45
        bne ds_next
        lda #1
        bne ds_val
ds_disk:
        lda #0
ds_val: sta dt_side
        ; a separator, or none before a digit
        iny
        cpy dt_len
        bcs ds_next
        lda (ptr),y
        cmp #$20
        beq +
        cmp #$5F
        beq +
        cmp #$A4
        beq +
        cmp #$2D
        beq +
        bne ++
+       iny
++      sty dt_j
        ; its value: up to 3 characters, 1 for a side; a disk's starts with a digit, or is one letter
        ldx #0
-       cpy dt_len
        bcs +
        lda (ptr),y
        jsr ds_alnum
        bcc +
        inx
        iny
        cpx #4
        bcc -
        jmp ds_next             ; too long: a word
+       cpx #0
        bne +
        jmp ds_next
+       lda dt_side
        beq +
        cpx #1
        beq ds_take
        jmp ds_next
+       cpx #1
        beq ds_take
        sty dt_y
        ldy dt_j
        lda (ptr),y
        ldy dt_y
        cmp #$30
        bcc ds_no
        cmp #$3A
        bcc ds_take
ds_no:  jmp ds_next
ds_take:
        sty dt_i                ; on from after it, for a side
        ldy dt_j
-       lda (ptr),y
        jsr fold_up
        ldx dt_o
        cpx #4
        bcs +
        sta dt_b,x
        inc dt_o
+       iny
        cpy dt_i
        bcc -
        jmp ds_scan
ds_done:
        ldx dt_o
        beq dt_last
        txa
        eor #$FF
        sec
        adc #40
        sta dt_d
        ldx #0
-       lda dt_b,x
        jsr fmt_char
        ldy dt_d
        sta rowbuf,y
        inc dt_d
        inx
        cpx dt_o
        bne -
        rts
; C set if A is a letter or digit, of either alphabet
ds_alnum:
        jsr fold_up
        cmp #$30
        bcc ++
        cmp #$3A
        bcc +
        cmp #$41
        bcc ++
        cmp #$5B
        bcs ++
+       sec
        rts
++      clc
        rts
dt_last:
        ldy #E_DLEN
        lda (ptr),y
        tay
        dey                     ; "(System 3)" is not a disk number
        bmi +
        lda (ptr),y
        cmp #$29
        beq dt_out
        cmp #$5D
        beq dt_out
+       ldy #E_DLEN
        lda (ptr),y
        tay
        ldx #0
-       dey
        bmi dt_out
        lda (ptr),y
        cmp #$5F
        beq dt_sep
        cmp #$20
        beq dt_sep
        cmp #$2D
        beq dt_sep
        inx
        cpx #5
        bcc -
dt_out: rts
dt_sep: cpx #0
        beq dt_out
        sty dt_y
        stx dt_n
        cpx #1
        beq dt_show
-       iny                     ; a digit in it?
        dex
        bmi dt_out
        lda (ptr),y
        cmp #$30
        bcc -
        cmp #$3A
        bcs -
dt_show:
        lda #40
        sec
        sbc dt_n
        sta dt_d
-       inc dt_y
        ldy dt_y
        lda (ptr),y
        jsr list_code
        ldy dt_d
        sta rowbuf,y
        inc dt_d
        dec dt_n
        bne -
        rts

d_size:
        ldy #E_BLK
        lda (ptr),y
        sta d_val
        iny
        lda (ptr),y
        sta d_val+1
        ldx #39
        jmp decimal

; the format's letters are always plain capitals
fmt_char:
        stx dp_x
        cmp #$20
        beq +
        cmp #$5B
        bcc +
        lda #$3F
+       ldx dp_x
        ; falls into uc_fix

; a mixed-set capital as the uppercase set's, when the list rows use that set. Only A and the flags
uc_fix: bit ucase
        bpl +
        cmp #$41
        bcc +
        cmp #$5B
        bcs +
        and #$1F
+       rts

; the name, columns 3-30; a long one keeps its start and end
draw_name:
        ldy #E_DLEN
        lda (ptr),y
        sta d_len
        cmp #NAME_W+1
        bcs dn_long
        ldy #0
-       cpy d_len
        beq dn_out
        sty dp_y
        lda (ptr),y
        jsr list_code
        ldy dp_y
        sta rowbuf+3,y
        iny
        bne -
dn_out: rts
dn_long:
        ldy #0
-       sty dp_y
        lda (ptr),y
        jsr list_code
        ldy dp_y
        sta rowbuf+3,y
        iny
        cpy #NAME_W-10
        bne -
        lda #$2E
        sta rowbuf+3,y
        sta rowbuf+4,y
        ; the last 8
        sec
        lda d_len
        sbc #8
        tay
        ldx #0
-       sty dp_y
        stx d_row2
        lda (ptr),y
        jsr list_code
        ldx d_row2
        sta rowbuf+3+NAME_W-8,x
        ldy dp_y
        iny
        inx
        cpx #8
        bne -
        rts

; d_val (16 bits) as decimal into rowbuf, ending at column X
decimal:
--      lda #0                  ; d_val /= 10, the remainder in A
        ldy #16
-       asl d_val
        rol d_val+1
        rol
        cmp #10
        bcc +
        sbc #10
        inc d_val
+       dey
        bne -
        ora #$30
        sta rowbuf,x
        dex
        lda d_val
        ora d_val+1
        bne --
        rts

; row 1: the path, shortened from the left, and where we are
draw_path:
        ldy #39
        lda #$20
-       sta rowbuf,y
        dey
        bpl -
        lda pathlen             ; the root: /
        bne +
        lda #$2F
        sta rowbuf+1
        jmp dp_pos
+       sec                     ; the last 25 characters
        sbc #25
        bcs +
        lda #0
+       tax
        ldy #1
        cpx #0
        beq +
        lda #$2E                ; shortened: two dots
        sta rowbuf+1
        sta rowbuf+2
        ldy #3
+
-       cpx pathlen
        beq dp_pos
        lda pathbuf,x
        stx dp_x
        sty dp_y
        ldy fwmode              ; a plain drive's names are PETSCII
        bne +
        jsr sc_pet
        jmp ++
+       jsr sc_ascii
++
        ldy dp_y
        sta rowbuf,y
        ldx dp_x
        inx
        iny
        bne -
dp_pos:
        ; "12 OF 340", right-aligned: the back row is not counted
        lda #0
        sta dp_up
        lda flen
        bne +
        lda pathlen
        beq +
        inc dp_up               ; (a back row, entry 0, is on the list)
+       lda big
        beq +
        lda total               ; a big folder: the firmware's count
        sta d_val
        lda total+1
        sta d_val+1
        jmp ++
+       sec
        lda nvis
        sbc dp_up
        sta d_val
        lda nvis+1
        sbc #0
        sta d_val+1
++      lda d_val               ; nothing listed: just the 0
        ora d_val+1
        pha
        ldx #38
        jsr decimal
        pla
        beq dp_show
        lda dp_up               ; on the back row: only the total
        beq +
        lda cur
        ora cur+1
        beq dp_show
+       ldy #3                  ; " OF "
-       lda t_of,y
        sta rowbuf,x
        dex
        dey
        bpl -
        ; the row number
        lda big
        beq +
        clc
        lda cur
        adc w_start
        sta d_val
        lda cur+1
        adc w_start+1
        sta d_val+1
        jmp ++
+       lda cur
        sta d_val
        lda cur+1
        sta d_val+1
++      sec
        lda d_val
        sbc dp_up
        sta d_val
        lda d_val+1
        sbc #0
        sta d_val+1
        inc d_val               ; counting from 1
        bne ++
        inc d_val+1
++      jsr decimal
dp_show:
        ldx #39
-       lda rowbuf,x
        sta SCREEN+40*ROW_PATH,x
        lda #C_TEXT
        sta COLOUR+40*ROW_PATH,x
        dex
        bpl -
        rts

t_of:   !scr " of "

; row 23: what the keys do now
draw_foot:
        jsr selected
        bcc +
        lda #T_NOTHING
+       tax
        lda footlo,x
        sta sptr
        lda foothi,x
        sta sptr+1
        ldy #39
-       lda (sptr),y
        sta SCREEN+40*ROW_FOOT,y
        lda #C_HEAD
        sta COLOUR+40*ROW_FOOT,y
        dey
        bpl -
        rts

footlo: !byte <f_prg, <f_dir, <f_other, <f_up, <f_disk, <f_none
foothi: !byte >f_prg, >f_dir, >f_other, >f_up, >f_disk, >f_none
f_prg:   !scr " Return run   F1 options   Del back     "
f_dir:   !scr " Return open   Del back                 "
f_other: !scr " Can't run from here.  Del back         "
f_up:    !scr " Return back                            "
f_disk:  !scr " Return open   F1 options   Del back    "
f_none:  !scr " Del back                               "

; row 24: the filter, or how to start one
draw_filter:
        ldy #39
        lda #$20
-       sta rowbuf,y
        dey
        bpl -
        lda err
        beq +
        ldx #0
-       lda t_err,x
        bne *+5
        jmp df_put
        sta rowbuf+1,x
        inx
        bne -
+       lda flen
        bne df_filter
        ; a name too long for its row: the whole of it here, with the cursor on it
        jsr selected
        bcs df_notlong
        ldy #E_LEN
        lda (ptr),y
        cmp #NAME_W+1
        bcc df_notlong
        sta d_len
        ldy #0
-       sty dp_y
        lda (ptr),y
        jsr screen_code
        ldy dp_y
        sta rowbuf+1,y
        iny
        cpy d_len
        beq df_put_j
        cpy #38
        bne -
df_put_j:
        jmp df_put
df_notlong:
        lda cut
        beq +
        ldx #0
-       lda t_cut,x
        beq df_put_j
        sta rowbuf+1,x
        inx
        bne -
+       ldx #0
-       lda t_hint,x
        beq df_put_j
        sta rowbuf+1,x
        inx
        bne -
df_filter:
        ldx #0
-       lda t_filt,x
        beq +
        sta rowbuf+1,x
        inx
        bne -
+       ldy #0
-       cpy flen
        beq +
        lda fbuf,y
        stx dp_x
        sty dp_y
        jsr sc_pet
        ldy dp_y
        ldx dp_x
        sta rowbuf+1,x
        inx
        iny
        bne -
+       lda #$A0                ; a block cursor after it
        sta rowbuf+1,x
        lda nvis
        ora nvis+1
        bne df_put
        ldx #0                  ; nothing matches: say so
-       lda t_nomatch,x
        beq df_put
        sta rowbuf+27,x
        inx
        bne -
df_put:
        ldx #39
-       lda rowbuf,x
        sta SCREEN+40*ROW_FILTER,x
        lda #C_TEXT
        sta COLOUR+40*ROW_FILTER,x
        dex
        bpl -
        rts

t_hint:    !scr "Type to filter  Run/Stop quit  F3 turbo",0
t_filt:    !scr "Filter: ",0
t_nomatch: !scr "No match",0
t_err:     !scr "The drive is not answering",0
t_cut:     !scr "Showing the first 2048: type to filter",0

; the screen's rows
scrlo:
        !for i, 0, 24 { !byte <(SCREEN + 40 * i) }
scrhi:
        !for i, 0, 24 { !byte >(SCREEN + 40 * i) }

; ---------------------------------------------------------------------------
; variables (not saved)
cur:        !word 0
top:        !word 0
nvis:       !word 0
count:      !word 0
err:        !byte 0
flen:       !byte 0
fbuf:       !fill 16, 0
f_from:     !word 0
f_to:       !word 0
f_val:      !word 0
nm_at:      !byte 0
nm_last:    !byte 0
j_now:      !byte 0
j_fire:     !byte 0
j_start:    !byte 0
j_held:     !byte 0
j_time:     !byte 0
st:         !byte 0
first_line: !byte 0
l_state:    !byte 0
l_len:      !byte 0
l_type:     !byte 0
l_blk:      !word 0
l_run:      !byte 0
cl_a:       !byte 0
cl_b:       !byte 0
cl_c:       !byte 0
cl_len:     !byte 0
disklen:    !byte 0
diskname:   !fill 16, 0
d_i:        !word 0
d_row:      !byte 0
d_col:      !byte 0
d_len:      !byte 0
d_val:      !word 0
dp_x:       !byte 0
dp_y:       !byte 0
dp_up:      !byte 0
m_items:    !word 0
m_n:        !byte 0
m_cur:      !byte 0
m_i:        !byte 0
m_x:        !byte 0
m_r:        !byte 0
ucase:  !byte 0          ; $80: the list rows are a drive's listing, drawn in the uppercase/graphics set
split:  !byte 0          ; 1: the raster split that shows them so is on
splitst: !byte 0
hostfs:     !byte 0
vdisk:      !byte 0
br_fail:    !byte 0
dt_y:       !byte 0
dt_n:       !byte 0
dt_d:       !byte 0
dt_len:     !byte 0
dt_o:       !byte 0
dt_i:       !byte 0
dt_j:       !byte 0
dt_w:       !fill 2, 0
dt_side:    !byte 0
dt_b:       !fill 4, 0
lastlen:    !byte 0
lastname:   !fill 16, 0
pathlen:    !byte 0
pathover:   !byte 0
pathtop:    !byte 0
pathbuf:    !fill PATHMAX, 0
rowbuf:     !fill 40, 0
cmdbuf:     !fill 40, 0
turbo_on:   !byte 0
wf_n:       !byte 0
fwcap:      !byte 0
fwmode:     !byte 0
big:        !byte 0
cut:        !byte 0
total:      !word 0
sel:        !word 0
w_start:    !word 0
w_n:        !word 0
w_want:     !word 0
wc_a:       !word 0
d_row2:     !byte 0
slow_once:  !byte 0
rf_noup:    !byte 0                 ; RUN FIRST went by XI: no CD_ before XM
tt_y:       !byte 0
t_namelen:  !byte 0
t_name:     !fill 48, 0
t_addr:     !word 0
t_size:     !word 0
t_end:      !word 0
T_REPLY = 32
t_reply:    !fill T_REPLY, 0
end_of_program:
!if end_of_program >= TABLE { !error "the program has reached the entry table" }
