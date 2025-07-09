#include "esp_crc.h"

static uint32_t crc32_table[256];

// Call once before use:
void crc32_init() {
    uint32_t polynomial = 0xEDB88320;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (polynomial ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
}

uint32_t esp_crc32_le(uint32_t crc, const uint8_t *buf, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        uint8_t index = (uint8_t)((crc ^ buf[i]) & 0xFF);
        crc = (crc >> 8) ^ crc32_table[index];
    }
    return ~crc;
}