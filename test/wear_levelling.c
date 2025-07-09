#include "wear_levelling.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#define VIRTUAL_FLASH_PATH "test/data/virtual_flash.bin"
#define SNAPSHOTS_DIR "test/data/snapshots"

static struct {
    int fd;
    size_t sector_size;
    size_t sector_count;
} instance = {
    .fd = -1,
    .sector_size = 4096,
    .sector_count = 128,
};

static int save_snapshots = 1;
static int snapshot_index = 0;

#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>

#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

const esp_partition_t* esp_partition_find_first(int type, int subtype, const char* label) {
    static esp_partition_t mock_partition;
    static int initialized = 0;
    if (!initialized) {
        mock_partition.label = label ? strdup(label) : "mock_partition";
        initialized = 1;
    }
    return &mock_partition;
}

int ensure_dir_exists(const char *path) {
    struct stat st = {0};
    int res = stat(path, &st);
    if (res == -1) {
        return mkdir(path, 0755);
    }
    return 0;
}

void clear_dir(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name);
        unlink(filepath);
    }
    closedir(dir);
}

static void save_snapshot() {
    fsync(instance.fd);
    char snapshot_name[64];
    snprintf(snapshot_name, sizeof(snapshot_name), SNAPSHOTS_DIR "/virtual_flash_snapshot_%05d.bin", snapshot_index++);

    FILE *src = fopen(VIRTUAL_FLASH_PATH, "rb");
    if (!src) return;

    FILE *dst = fopen(snapshot_name, "wb");
    if (!dst) {
        fclose(src);
        return;
    }

    char buffer[4096];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        fwrite(buffer, 1, bytes, dst);
    }

    fclose(src);
    fclose(dst);
}

esp_err_t wl_mount(const esp_partition_t *partition, wl_handle_t *out_handle) {
    if (!partition || !out_handle) return ESP_ERR_INVALID_ARG;

    int e = ensure_dir_exists("test/data");
    clear_dir("test/data");

    instance.fd = open(VIRTUAL_FLASH_PATH, O_RDWR | O_CREAT, 0666);
    if (instance.fd < 0) return ESP_FAIL;

    size_t total_size = instance.sector_size * instance.sector_count;
    ftruncate(instance.fd, total_size);

    // Initialize with 0xFF
    uint8_t *blank = (uint8_t* )malloc(instance.sector_size);
    memset(blank, 0xFF, instance.sector_size);
    for (size_t i = 0; i < instance.sector_count; ++i)
        pwrite(instance.fd, blank, instance.sector_size, i * instance.sector_size);
    free(blank);

    *out_handle = 0;
    
    e = ensure_dir_exists(SNAPSHOTS_DIR);
    clear_dir(SNAPSHOTS_DIR);
    return ESP_OK;
}

esp_err_t wl_unmount(wl_handle_t handle) {
    if (instance.fd >= 0) close(instance.fd);
    instance.fd = -1;
    return ESP_OK;
}

esp_err_t wl_read(wl_handle_t handle, size_t addr, void *dest, size_t size) {
    if (!dest || addr + size > wl_size(handle)) return ESP_ERR_INVALID_SIZE;
    if (pread(instance.fd, dest, size, addr) != (ssize_t)size) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t wl_write(wl_handle_t handle, size_t addr, const void *src, size_t size) {
    if (!src || addr + size > wl_size(handle)) return ESP_ERR_INVALID_SIZE;

    uint8_t *existing = (uint8_t *)malloc(size);
    if (!existing) return ESP_ERR_NO_MEM;
    if (pread(instance.fd, existing, size, addr) != (ssize_t)size) {
        free(existing);
        return ESP_FAIL;
    }

    const uint8_t *new_data = (const uint8_t*)src;
    uint8_t *masked_data = (uint8_t *)malloc(size);
    if (!masked_data) {
        free(existing);
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < size; ++i) {
        masked_data[i] = existing[i] & new_data[i];
    }

    free(existing);

    if (pwrite(instance.fd, masked_data, size, addr) != (ssize_t)size) {
        free(masked_data);
        return ESP_FAIL;
    }

    free(masked_data);

    if (save_snapshots) save_snapshot();

    return ESP_OK;
}

esp_err_t wl_erase_range(wl_handle_t handle, size_t start_addr, size_t size) {
    if (start_addr % instance.sector_size != 0 || size % instance.sector_size != 0)
        return ESP_ERR_INVALID_ARG;
    if (start_addr + size > wl_size(handle)) return ESP_ERR_INVALID_SIZE;

    uint8_t *buf = (uint8_t *)malloc(instance.sector_size);
    memset(buf, 0xFF, instance.sector_size);

    for (size_t i = 0; i < size; i += instance.sector_size) {
        if (pwrite(instance.fd, buf, instance.sector_size, start_addr + i) != (ssize_t)instance.sector_size) {
            free(buf);
            return ESP_FAIL;
        }
    }
    free(buf);
    
    if (save_snapshots) save_snapshot();

    return ESP_OK;
}

size_t wl_size(wl_handle_t handle) {
    return instance.sector_size * instance.sector_count;
}

size_t wl_sector_size(wl_handle_t handle) {
    return instance.sector_size;
}
