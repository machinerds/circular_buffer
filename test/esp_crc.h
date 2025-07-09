#pragma once

#include <stdint.h>
#include <stddef.h>

void crc32_init();

uint32_t esp_crc32_le(uint32_t crc, const uint8_t *buf, size_t len);