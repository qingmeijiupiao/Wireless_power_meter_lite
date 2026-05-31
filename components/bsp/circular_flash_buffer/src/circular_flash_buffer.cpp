/*
 * @LastEditors: qingmeijiupiao
 * @Description: 基于ESP32 SPI Flash分区的循环缓冲区驱动实现
 * @Author: qingmeijiupiao
 */
#include "circular_flash_buffer.h"
#include <stddef.h>
#include "esp_partition.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstring>

using namespace CircularFlashBuffer;

static esp_partition_t* cfb_partition = nullptr;
static size_t cfb_block_size = 0;
static uint8_t cfb_blocks_per_page = 0;

static bool cfb_enable = true;
static SemaphoreHandle_t cfb_mutex = nullptr;

static uint32_t now_write_page = 0;
static uint8_t block_page_index = 0;
static uint32_t block_count = 0;
static uint32_t max_retained_blocks = 0;
static uint32_t total_pages = 0;
static uint32_t total_sectors = 0;

static uint8_t page_buffer[PAGE_SIZE];

static esp_err_t erase_sector(uint32_t sector_index) {
    if (sector_index >= total_sectors) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_partition_erase_range(cfb_partition, sector_index * SECTOR_SIZE, SECTOR_SIZE);
}

static uint32_t count_valid_blocks_in_sector(uint32_t sector_index) {
    uint8_t buffer[PAGE_SIZE];
    uint32_t count = 0;
    const uint32_t first_page = sector_index * PAGES_PER_SECTOR;

    for (uint32_t page = 0; page < PAGES_PER_SECTOR; ++page) {
        if (esp_partition_read(cfb_partition, (first_page + page) * PAGE_SIZE, buffer, PAGE_SIZE) != ESP_OK) {
            return 0;
        }
        for (uint8_t block = 0; block < cfb_blocks_per_page; ++block) {
            if (buffer[block * cfb_block_size] == BLOCK_SOF) {
                ++count;
            }
        }
    }
    return count;
}

static esp_err_t write_page(uint32_t page_index, const uint8_t* data) {
    if (page_index >= total_pages) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_partition_write(cfb_partition, page_index * PAGE_SIZE, data, PAGE_SIZE);
}

static uint8_t get_empty_block_index_from_page(const uint8_t* page_data) {
    for (size_t i = 0; i < cfb_blocks_per_page; i++) {
        if (page_data[i * cfb_block_size] != BLOCK_SOF) {
            return i;
        }
    }
    return cfb_blocks_per_page;
}

esp_err_t CircularFlashBuffer::init(const char* partition_name, size_t block_size) {
    if (block_size == 0 || PAGE_SIZE % block_size != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cfb_block_size = block_size;
    cfb_blocks_per_page = PAGE_SIZE / block_size;

    alignas(4) uint8_t buffer[PAGE_SIZE];

    const esp_partition_t* _partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, partition_name);
    if (_partition == nullptr) {
        while (1) {
            ESP_LOGE("CircularFlashBuffer", "Partition not found!");
            vTaskDelay(1000);
        }
    }

    cfb_partition = (esp_partition_t*)_partition;
    total_pages = cfb_partition->size / PAGE_SIZE;
    total_sectors = cfb_partition->size / SECTOR_SIZE;
    if (total_sectors < 2) {
        return ESP_ERR_INVALID_SIZE;
    }
    max_retained_blocks = (total_sectors - 1) * SECTOR_SIZE / cfb_block_size;
    memset(page_buffer, 0xFF, PAGE_SIZE);

    bool empty_page_found = false;
    for (uint32_t page = 0; page < total_pages; ++page) {
        esp_partition_read(cfb_partition, page * PAGE_SIZE, buffer, PAGE_SIZE);
        if (!memcmp(buffer, page_buffer, PAGE_SIZE)) {
            now_write_page = page;
            empty_page_found = true;
            break;
        }
    }

    if (!empty_page_found) {
        ESP_LOGE("CircularFlashBuffer", "No empty page found! Flash may be worn out.");
        return ESP_ERR_INVALID_STATE;
    }

    if (now_write_page == 0) {
        esp_partition_read(cfb_partition, (total_pages - 1) * PAGE_SIZE, buffer, PAGE_SIZE);
        bool max_page_empty_1 = !memcmp(buffer, page_buffer, PAGE_SIZE);
        esp_partition_read(cfb_partition, (total_pages - 2) * PAGE_SIZE, buffer, PAGE_SIZE);
        bool max_page_empty_2 = !memcmp(buffer, page_buffer, PAGE_SIZE);
        if (max_page_empty_2 && max_page_empty_1) {
            now_write_page = 0;
            block_page_index = 0;
        } else if (!max_page_empty_2 && max_page_empty_1) {
            now_write_page = total_pages - 2;
            block_page_index = 0;
        } else if (!max_page_empty_2 && !max_page_empty_1) {
            now_write_page = total_pages - 1;
            if (get_empty_block_index_from_page(buffer) == cfb_blocks_per_page) {
                now_write_page = total_pages - 1;
            }
        }
    } else {
        esp_partition_read(cfb_partition, (now_write_page - 1) * PAGE_SIZE, buffer, PAGE_SIZE);
        uint8_t index = get_empty_block_index_from_page(buffer);
        if (index != cfb_blocks_per_page) {
            now_write_page = now_write_page - 1;
            block_page_index = index;
        } else {
            block_page_index = 0;
        }
    }

    block_count = 0;
    for (uint32_t sector = 0; sector < total_sectors; ++sector) {
        block_count += count_valid_blocks_in_sector(sector);
    }
    if (block_count > max_retained_blocks) {
        block_count = max_retained_blocks;
    }

    esp_partition_read(cfb_partition, now_write_page * PAGE_SIZE, buffer, PAGE_SIZE);
    memcpy(page_buffer, buffer, PAGE_SIZE);

    cfb_mutex = xSemaphoreCreateBinary();
    if (cfb_mutex == nullptr) {
        ESP_LOGE("CircularFlashBuffer", "Failed to create mutex!");
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(cfb_mutex);

    return ESP_OK;
}

esp_err_t CircularFlashBuffer::write_block(const uint8_t* data) {
    if (data == nullptr || cfb_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!cfb_enable) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    xSemaphoreTake(cfb_mutex, portMAX_DELAY);

    size_t offset = block_page_index * cfb_block_size;
    memcpy(page_buffer + offset, data, cfb_block_size);
    block_page_index = (block_page_index + 1) % cfb_blocks_per_page;
    if (block_count < max_retained_blocks) {
        block_count++;
    }

    if (write_page(now_write_page, page_buffer) != ESP_OK) {
        ESP_LOGE("CircularFlashBuffer", "Failed to write page %d", now_write_page);
        xSemaphoreGive(cfb_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    if (block_page_index == 0) {
        now_write_page = (now_write_page + 1) % total_pages;
        memset(page_buffer, 0xFF, PAGE_SIZE);

        if (now_write_page % PAGES_PER_SECTOR == 0) {
            uint32_t sector_index = now_write_page / PAGES_PER_SECTOR;
            uint32_t erase_sector_index = (sector_index + 1) % total_sectors;
            uint32_t erased_blocks = count_valid_blocks_in_sector(erase_sector_index);
            if (erase_sector(erase_sector_index) != ESP_OK) {
                ESP_LOGE("CircularFlashBuffer", "Failed to erase sector %d", erase_sector_index);
                xSemaphoreGive(cfb_mutex);
                return ESP_ERR_INVALID_STATE;
            }
            block_count = erased_blocks > block_count ? 0 : block_count - erased_blocks;
        }
    }

    xSemaphoreGive(cfb_mutex);
    return ESP_OK;
}

esp_err_t CircularFlashBuffer::read_block(uint32_t index, uint8_t* data) {
    if (data == nullptr || cfb_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(cfb_mutex, portMAX_DELAY);

    if (index >= block_count) {
        ESP_LOGW("CircularFlashBuffer", "Index out of range!");
        xSemaphoreGive(cfb_mutex);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer[PAGE_SIZE];

    uint32_t last_page;
    uint8_t last_offset;

    if (block_page_index == 0) {
        last_page = (now_write_page == 0) ? (total_pages - 1) : (now_write_page - 1);
        last_offset = cfb_blocks_per_page - 1;
    } else {
        last_page = now_write_page;
        last_offset = block_page_index - 1;
    }

    uint32_t target_page = last_page;
    uint8_t target_offset = last_offset;
    uint32_t remaining = index;

    while (remaining > 0) {
        if (target_offset >= remaining) {
            target_offset -= remaining;
            remaining = 0;
        } else {
            remaining -= (target_offset + 1);
            if (target_page == 0) {
                target_page = total_pages - 1;
            } else {
                target_page--;
            }
            target_offset = cfb_blocks_per_page - 1;
        }
    }

    esp_err_t err = esp_partition_read(cfb_partition,
                                       target_page * PAGE_SIZE,
                                       buffer,
                                       PAGE_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE("CircularFlashBuffer", "Failed to read page %d", target_page);
        xSemaphoreGive(cfb_mutex);
        return err;
    }

    memcpy(data, buffer + target_offset * cfb_block_size, cfb_block_size);

    xSemaphoreGive(cfb_mutex);
    return ESP_OK;
}

uint32_t CircularFlashBuffer::get_count() {
    return block_count;
}

void CircularFlashBuffer::set_enable(bool enable) {
    if (cfb_mutex == nullptr) {
        return;
    }
    xSemaphoreTake(cfb_mutex, portMAX_DELAY);
    cfb_enable = enable;
    xSemaphoreGive(cfb_mutex);
}
