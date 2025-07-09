#include "circular_buffer.h"
#include <cstdio>
#include <cstring>

#include "esp_crc.h"

#define ESP_ERROR_CHECK(x) do {                                         \
        esp_err_t err_rc_ = (x);                                        \
        (void) sizeof(err_rc_);                                         \
    } while(0)

int main() {
    crc32_init();

    wl_handle_t handle;
    esp_partition_t part = { .label = "mock" };
    ESP_ERROR_CHECK(wl_mount(&part, &handle));

    CircularBuffer cb;
    const size_t RECORD_SIZE = 16;
    ESP_ERROR_CHECK(cb.init((char*)"mock", RECORD_SIZE, true, true));

    uint8_t input[RECORD_SIZE], output[RECORD_SIZE];

    // Fill buffer with 10 records
    for (int i = 0; i < 10; i++) {
        memset(input, i, RECORD_SIZE);
        ESP_ERROR_CHECK(cb.push_back(input));
    }

    printf("Records in buffer: %u\n", cb.get_record_num());

    // Read & pop records
    for (int i = 0; i < 15; i++) {
        esp_err_t err = cb.pop_front(output);
        if (err != ESP_OK) {
            printf("Error at record %d: %d\n", i, err);
        } else {
            printf("Record %d:", i);
            for (int j = 0; j < RECORD_SIZE; j++) printf(" %02X", output[j]);
            printf("\n");
        }
    }

    ESP_ERROR_CHECK(wl_unmount(handle));
    return 0;
}
