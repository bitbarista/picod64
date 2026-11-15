PROG = bytes.fromhex(
 "78" "A9EE" "8D0C1C" "A200"            # SEI; read mode, SO on; X=0
 "2C001C" "30FB"                         # 0508: wait for SYNC (PB7 low)
 "AD0818" "9D0006" "AD0918" "9D0007"     # T2 lo/hi -> $0600,X / $0700,X
 "A9FF" "8D0818" "8D0918"                # restart T2 at $FFFF
 "2C001C" "10FB"                         # wait for the SYNC's end
 "B8" "50FE" "AD011C" "9D0004"           # the first byte after it -> $0400,X
 "E8" "E060" "D0D4" "4C3405")            # 96 SYNCs, then stop
