#include "circular_buffer.h"

#define MAGIC 0x5B15B1

size_t CircularBuffer::secs_for_header() {
    size_t sec_size = wl_sector_size(wl_handle);
    return 2 * ((sizeof(cb_header) + sec_size - 1) / sec_size);
}

size_t CircularBuffer::header_offset() { return secs_for_header() * wl_sector_size(wl_handle); }

esp_err_t CircularBuffer::write_header() {
    cb_header header;
    header.magic = MAGIC;
    header.front = front;
    header.record_num = record_num;
    esp_err_t err = wl_erase_range(wl_handle, 0, wl_sector_size(wl_handle) * secs_for_header());
    if (err != ESP_OK) { return err; }
    return wl_write(wl_handle, 0, &header, sizeof(header));
}

/**
 * Initializes circular buffer
 * @param partition_name name of partition in which circular buffer is going to be initialized
 * @param record_size  size of every record in circular buffer
 * @param overwrite whether overwrite feature should be turned on
 * @return ESP_OK if ok
 */
esp_err_t CircularBuffer::init(char* partition_name, size_t record_size, bool overwrite) {
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY,
        partition_name
    );

    if (partition == NULL) { return ESP_ERR_NOT_FOUND; }

    esp_err_t err = wl_mount(partition, &wl_handle);
    if (err != ESP_OK) { return err; }

    if (record_size > wl_sector_size(wl_handle)) { return ESP_ERR_INVALID_SIZE; }

    cb_header header;
    err = wl_read(wl_handle, 0, &header, sizeof(cb_header));
    if (err != ESP_OK) { return err; }

    if (header.magic != MAGIC) {
        front = 0;
        record_num = 0;
        write_header();
    } else {
        front = header.front;
        record_num = header.record_num;
    }

    this->record_size = record_size;
    this->overwrite = overwrite;

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
long unsigned int CircularBuffer::get_max_records() { return sec_num() * records_in_sec(); }

uint32_t CircularBuffer::sec_num() { return wl_size(wl_handle) / wl_sector_size(wl_handle) - secs_for_header(); }