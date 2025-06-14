#include "circular_buffer.h"

#include "esp_crc.h"

#define MAGIC 0x5B15B1

bool is_all_ff(const void *ptr, size_t len) {
    const uint8_t *p = (const uint8_t *)ptr;
    for (size_t i = 0; i < len; i++) {
        if (p[i] != 0xFF) return false;
    }
    return true;
}

void update_crc(struct cb_header *hdr) {
    hdr->crc = 0;
    hdr->crc = esp_crc32_le(0, (const uint8_t*)hdr, sizeof(struct cb_header) - sizeof(uint32_t));
}

bool check_header(const struct cb_header *hdr) {
    uint32_t crc = esp_crc32_le(0, (const uint8_t*)hdr, sizeof(struct cb_header) - sizeof(uint32_t));
    return crc == hdr->crc && hdr->magic == MAGIC;
}

size_t CircularBuffer::secs_for_one_header() {
    size_t sec_size = wl_sector_size(wl_handle);
    return (sizeof(cb_header) + sec_size - 1) / sec_size;
}

size_t CircularBuffer::secs_for_header() {
    return 2 * secs_for_one_header();
}

size_t CircularBuffer::header_offset() { return secs_for_header() * wl_sector_size(wl_handle); }

esp_err_t CircularBuffer::write_header() {
    cb_header header;
    header.magic = MAGIC;
    header.front = front;
    header.record_num = record_num;
    header.sequence = ++sequence;
    update_crc(&header);
    size_t addr = (sequence % 2) * secs_for_one_header() * wl_sector_size(wl_handle);
    esp_err_t err = wl_erase_range(wl_handle, addr, secs_for_one_header() * wl_sector_size(wl_handle));
    if (err != ESP_OK) { return err; }
    return wl_write(wl_handle, addr, &header, sizeof(header));
}

/**
 * Initializes circular buffer
 * @param partition_name name of partition in which circular buffer is going to be initialized
 * @param record_size  size of every record in circular buffer
 * @param overwrite whether overwrite feature should be turned on
 * @param recovery_mode use backup header if header was correupted (this may cause in at most one corrupted record)
 * @return ESP_OK if ok
 */
esp_err_t CircularBuffer::init(char* partition_name, size_t record_size, bool overwrite, bool recovery_mode) {
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY,
        partition_name
    );

    if (partition == NULL) { return ESP_ERR_NOT_FOUND; }

    esp_err_t err = wl_mount(partition, &wl_handle);
    if (err != ESP_OK) { return err; }

    size_t sec_size = wl_sector_size(wl_handle);

    if (record_size > sec_size) { return ESP_ERR_INVALID_SIZE; }

    this->record_size = record_size;
    this->overwrite = overwrite;

    cb_header header1;
    err = wl_read(wl_handle, 0, &header1, sizeof(cb_header));
    if (err != ESP_OK) { return err; }

    cb_header header2;
    err = wl_read(wl_handle, secs_for_one_header() * sec_size, &header2, sizeof(cb_header));
    if (err != ESP_OK) { return err; }

    bool header1_valid = check_header(&header1);
    bool header2_valid = check_header(&header2);

    if (header1_valid && header2_valid) {
        if (header1.sequence > header2.sequence || (header1.sequence == 0 && header2.sequence == UINT32_MAX)) {
            front = header1.front;
            record_num = header1.record_num;
            sequence = header1.sequence;
        } else {
            front = header2.front;
            record_num = header2.record_num;
            sequence = header2.sequence;
        }
    } else if (recovery_mode && (header1_valid ^ header2_valid)) {
        if (header1_valid) {
            front = header1.front;
            record_num = header1.record_num;
            sequence = header1.sequence;
        } else {
            front = header2.front;
            record_num = header2.record_num;
            sequence = header2.sequence;
        }
        size_t back = get_back();
        if (back % sec_size != 0) {
            void* next = malloc(record_size);
            wl_read(wl_handle, header_offset() + back, next, record_size);
            if (!is_all_ff(next, record_size)) {
                ++record_num;
                write_header();
            }
            free(next);
        }
    } else {
        front = 0;
        record_num = 0;
        sequence = -1;
        write_header();
    }

    return ESP_OK;
}

/**
 * Pushes data to the back of circular buffer
 * @param src source of data
 * @return ESP_OK if ok
 */
esp_err_t CircularBuffer::push_back (void* src) {
    size_t sec_size = wl_sector_size(wl_handle);
    size_t back;
    uint32_t remaining_capacity_in_front_sector = (sec_size - (front % sec_size)) / record_size;
    if (remaining_capacity_in_front_sector > record_num) { back = front + (record_num * record_size); }
    else {
        uint32_t remaining_records = record_num - remaining_capacity_in_front_sector;
        uint32_t full_secs = remaining_records / records_in_sec();
        uint32_t front_sec = front / sec_size;
        uint32_t back_sec = (front_sec + full_secs + 1) % sec_num();
        if (back_sec == front_sec) {
            if (overwrite) {
                front = ((front_sec + 1) % sec_num()) * sec_size;
                record_num -= remaining_capacity_in_front_sector;
            }
            else { return ESP_ERR_NO_MEM; }
        }
        size_t back_offset_in_sec = (remaining_records % records_in_sec()) * record_size;
        back = back_sec * sec_size + back_offset_in_sec;
    }
    if (back % sec_size == 0) { wl_erase_range(wl_handle, back + header_offset(), sec_size); }
    esp_err_t err = wl_write(wl_handle, back + header_offset(), src, record_size); 
    if (err != ESP_OK) { return err; }
    record_num++;
    return write_header();
}

/**
 * Retrieves data from the front of the circular buffer
 * @param dest destination of data
 * @return ESP_OK if ok
 */
esp_err_t CircularBuffer::peek_front (void* dest) {
    if (record_num == 0) { return ESP_ERR_NOT_FOUND; }
    return wl_read(wl_handle, front + header_offset(), dest, record_size);
}

/**
 * Retrieves data from the front of the circular buffer and deletes it
 * @param dest destination of data
 * @return ESP_OK if ok
 */
esp_err_t CircularBuffer::pop_front (void* dest) {
    esp_err_t err = peek_front(dest);
    if (err != ESP_OK) { return err; }
    return delete_front();
}

/**
 * @return Number of records currently in the circular buffer
 */
uint32_t CircularBuffer::get_record_num() { return record_num; }

/**
 * Deletes one record from the front of the circular buffer
 * @return ESP_OK if ok
 */
esp_err_t CircularBuffer::delete_front() {
    size_t sec_size = wl_sector_size(wl_handle);
    if (sec_size - (front % sec_size) > 2 * record_size) { front += record_size; }
    else { front = ((front / sec_size) + 1) % sec_num() * sec_size; }
    record_num--;
    return write_header();
}

size_t CircularBuffer::records_in_sec() { return wl_sector_size(wl_handle) / record_size; }

/**
 * @return Capacity of the circular buffer
 */

size_t CircularBuffer::get_max_records() { return sec_num() * records_in_sec(); }

uint32_t CircularBuffer::sec_num() { return wl_size(wl_handle) / wl_sector_size(wl_handle) - secs_for_header(); }

size_t CircularBuffer::get_back() {
    size_t sec_size = wl_sector_size(wl_handle);
    uint32_t remaining_capacity_in_front_sector = (sec_size - (front % sec_size)) / record_size;
    if (remaining_capacity_in_front_sector > record_num) { return front + (record_num * record_size); }
    else {
        uint32_t remaining_records = record_num - remaining_capacity_in_front_sector;
        uint32_t full_secs = remaining_records / records_in_sec();
        uint32_t front_sec = front / sec_size;
        uint32_t back_sec = (front_sec + full_secs + 1) % sec_num();
        size_t back_offset_in_sec = (remaining_records % records_in_sec()) * record_size;
        return back_sec * sec_size + back_offset_in_sec;
    }
}