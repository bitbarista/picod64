/* The run's fingerprint, printed identically by host/drive_test and the Pico
 * bench. Include after core/drive1541.h. */
#ifndef FF_FINGERPRINT_H
#define FF_FINGERPRINT_H
#include <stdint.h>

static uint32_t crc32_buf(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1)); }
    return ~c;
}

#define print_fingerprint(PF) \
    PF("fingerprint: cycles=%lu pc=%04X passes=%u irqs=%u bytes=%lu ram_crc=%08lX%s\n", \
       (unsigned long)D.cycles, CPU.pc, D.ram[0x05] | (D.ram[0x06] << 8), D.ram[0x08] | (D.ram[0x09] << 8), \
       (unsigned long)D.bytes_read, (unsigned long)crc32_buf(D.ram, sizeof D.ram), \
       CPU.jammed ? " JAMMED" : "")
#endif
