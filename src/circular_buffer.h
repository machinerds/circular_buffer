#include "wear_levelling.h"

struct cb_header {
    uint32_t magic;
    size_t front;
    uint32_t record_num;
};

class CircularBuffer {
    public:
        esp_err_t init(char* partition_name, size_t record_size, bool overwrite = false);
        esp_err_t push_back(void* src);
        esp_err_t peek_front(void* dest);
        esp_err_t pop_front(void* dest);
        esp_err_t delete_front();
        uint16_t get_record_num();
        long unsigned int get_max_records();
    private:
        size_t front;
        size_t record_size;
        size_t record_num;
        size_t records_in_sec();
        wl_handle_t wl_handle;
        size_t secs_for_header();
        esp_err_t write_header();
        uint16_t sec_num();
        size_t header_offset();
        bool overwrite = false;
};