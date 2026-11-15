# PicoD64 rev 1.1 — what to buy (one board)

**Farnell is the tested source.** The first boards were built from Farnell parts; upload [`upload-farnell.csv`](upload-farnell.csv)
(every part, optional ones included) at farnell.com. [`upload-lcsc-untested.csv`](upload-lcsc-untested.csv) is an LCSC alternative that has
**never been ordered**: where LCSC lacks a part it substitutes an equivalent, and the Pico 2 and the DIN sockets
aren't on LCSC at all (buy them elsewhere). Check it against this list before ordering. Both files are made by
`gen/bom_upload.py`. This page is the short list of fitted parts.

Fitted parts only. Not-fitted options are in picod64-bom.csv.

| Qty | Part | Farnell | Goes in |
|---|---|---|---|
| 1 | Raspberry Pi Pico 2 (SC1631), no headers. **Not the Pico 2 H** (headers and a debug connector fitted) | 4531086 | A1 |
| 2 | Lumberg 0105 06 DIN-6 socket | 2677786 | J1 J2 |
| 1 | GCT MEM2075-00-140-01-A microSD socket | 2895610 | J3 |
| 1 | Alps EC11E15244G1 encoder + push | 2064986 | SW1 |
| 2 | onsemi 2N3904BU | 1700648 | Q1 Q2 |
| 1 | Kingbright L-934GD LED 3 mm green diffused | 1142502 | D1 |
| 1 | Kingbright L-934ID LED 3 mm red diffused | 1142517 | D2 |
| 1 | 10 µF 50 V electrolytic, MCGPR50V106M5X11 | 9451382 | C1 |
| 2 | 100 nF ceramic, K104K15X7RF53L2 | 1141775 | C2 C3 |
| 2 | 10 nF ceramic, K103K15X7RF53L2 | 1141772 | C4 C5 |
| 5 | 6k8 resistor, MF25 6K8 | 9342168 | R1 R2 R3 R4 R15 |
| 4 | 10k resistor, MF25 10K | 9341110 | R6 R8 R11 R18 |
| 2 | 680R resistor, MF25 680R | 9342150 | R5 R7 |
| 2 | 470R resistor, MF25 470R | 9341943 | R12 R13 |
| 1 | 100R resistor, MF25 100R | 9341099 | R14 |
| 2 | 4k7 resistor, Multicomp Pro MF25 4K7 | 9341951 | R9 R10 |
| 1 | Bourns 4609X-101-103LF (10k × 8, SIP-9) | 9356819 | RN1 |
| 1 | Bourns 4605X-101-103LF (10k × 4, SIP-5) | 9356061 | RN2 |
| 1 | Bourns 4605X-101-102LF (1k × 4, SIP-5) | 9356053 | RN3 |
| 1 | Vishay 1N5819-E3/54 Schottky | 1336533 | D4 |
| 1 | TDK PS1240P02BT piezo (external drive) | 3267212 | BZ1 |
| 1 | Würth 61304011121 header strip 1×40, breakaway (cut 3+3+3) | 2356175 | J5 JP1 JP2 |
| 1 | Würth 61300411821 socket 1×4, 8.5 mm | 2827888 | J4 |
| 2 | Harwin M7566-05 jumper cap | 150411 | on JP1 JP2 |

Not from Farnell:

| Qty | Part |
|---|---|
| 1 | OLED 1.3" SH1106, I²C, 4-pin (held by printed pillars, no screws: BUILD-NOTES, OLED) |
| 4 | M2.6 × 16 countersunk self-tapping screw (case; or M3 × 16 machine screws + heat-set inserts: BUILD-NOTES, step 7) |
| 4 | 10 mm self-adhesive rubber feet |

Notes: get the resistors in 10s or 25s if that's the minimum (Farnell sells MF25 cut tape). Caps must be the **L2** suffix (2.5 mm leads), not TH5.
