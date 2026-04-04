/*
 * @version: no version
 * @LastEditors: qingmeijiupiao
 * @Description: 黑匣子实现
 * @author: qingmeijiupiao
 * @LastEditTime: 2026-04-05 00:11:11
 */
#include "blackbox.h"
#include <stddef.h>
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_flash.h"
#include <cstring>
using namespace BlackBox;
auto& global_state_blackbox = get_global_state();
auto& global_state_blackbox = get_global_state();
static esp_partition_t* blackbox_partition = nullptr;

static bool log_enable = true; // 是否启用日志记录

static uint32_t now_write_page = 0;   // 当前写入的页面索引
static uint8_t log_page_index = 0;    // 下一条日志在页面中的索引
static uint32_t log_count = 0;        // 已记录日志数量
static uint32_t total_pages = 0;      // 总页数
static uint32_t total_sectors = 0;    // 总扇区数

static uint8_t page_baffer[PAGE_SIZE]; // 页面缓冲区

/**
 * @brief : 计算CRC8校验码
 * note: 使用多项式 x^8 + x^2 + x + 1 (0x07)
 * @return  {uint8_t} - CRC8校验码
 * @param {uint8_t*} data - 输入数据指针
 * @param {size_t} length - 输入数据长度
 */
uint8_t CRC8_Calc(const uint8_t* data, size_t length) {
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; ++j) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07; // 多项式 x^8 + x^2 + x + 1
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief : 擦除指定扇区
 * @return  {esp_err_t} - 操作结果
 * @param {uint32_t} sector_index - 扇区索引
 */
esp_err_t erase_sector(uint32_t sector_index) {
    if (sector_index >= total_sectors) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_partition_erase_range(blackbox_partition, sector_index * SECTOR_SIZE, SECTOR_SIZE);
}

/**
 * @brief : 写入指定页面
 * @return  {esp_err_t} - 操作结果
 * @param {uint32_t} page_index - 页面索引
 * @param {uint8_t*} data - 输入数据指针
 */
esp_err_t write_page(uint32_t page_index, const uint8_t* data) {
    if (page_index >= total_pages) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_partition_write(blackbox_partition, page_index * PAGE_SIZE, data, PAGE_SIZE);
} 

/**
 * @brief : 从页面数据中获取第一个空位置索引
 * @return  {uint8_t} - 第一个空位置索引
 * @param {uint8_t*} page_data - 页面数据指针
 */
uint8_t get_empty_log_index_form_page(uint8_t* page_data){
    for (size_t i = 0; i < PAGE_SIZE/sizeof(BlackBoxData_t); i++){
        if(page_data[i*sizeof(BlackBoxData_t)] != DATA_SOF){// 找到第一个没有帧头的位置
            return i;
        }
    }
    return PAGE_SIZE/sizeof(BlackBoxData_t); // 没有找到没有帧头的位置，说明页面满了
}

esp_err_t BlackBox::init(){
    alignas(4) uint8_t buffer[PAGE_SIZE];
    
    const esp_partition_t* _partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "blackbox");   
    if (_partition == nullptr) {
        while(1){
            ESP_LOGE("BlackBox", "Partition not found!");
            vTaskDelay(1000);
        }
    }

    //初始化全局变量
    blackbox_partition = (esp_partition_t*)_partition;
    total_pages = blackbox_partition->size / PAGE_SIZE;
    total_sectors = blackbox_partition->size / SECTOR_SIZE;
    memset(page_baffer, 0xFF, PAGE_SIZE); // 初始化页面缓冲区为全0xFF

    bool empty_page_found = false;
    for (uint32_t page = 0; page < total_pages; ++page) {
        esp_partition_read(blackbox_partition, page * PAGE_SIZE, buffer, PAGE_SIZE);
        if (!memcmp(buffer, page_baffer, PAGE_SIZE)) {
            now_write_page = page; // 设置当前写入页面索引
            empty_page_found = true;
            break;
        }
    }

    //设计上至少有一个空页，如果没有，说明flash寿命已尽
    if(!empty_page_found){
        ESP_LOGE("BlackBox", "No empty page found! Flash may be worn out.");
        return ESP_ERR_INVALID_STATE;
    }

    // 第一个页面为空，可能是第一次使用，也可能是之前的日志已经写满了并且擦除了这个页面，需要判断情况
    if (now_write_page==0){ 
        esp_partition_read(blackbox_partition, (total_pages - 1) * PAGE_SIZE, buffer, PAGE_SIZE);
        bool max_page_empty_1 = !memcmp(buffer, page_baffer, PAGE_SIZE);
        esp_partition_read(blackbox_partition, (total_pages - 2) * PAGE_SIZE, buffer, PAGE_SIZE);
        bool max_page_empty_2 = !memcmp(buffer, page_baffer, PAGE_SIZE);       
        if (max_page_empty_2 && max_page_empty_1){ //新设备，之前没有写过日志
            now_write_page = 0;
            log_page_index = 0;
        }else if(!max_page_empty_2&& max_page_empty_1){ //max_page-1有数据，说明之前刚好写满max_page-1把max_page和page0擦除了，说明现在应该写入max_page-2
            now_write_page = total_pages - 2;
            log_page_index = 0;
        }else if(!max_page_empty_2 && !max_page_empty_1){ //max_page-1和max_page都有数据，说明之前刚好写满max_page把page0擦除了，说明现在应该写入max_page-1
            now_write_page = total_pages - 1;
            //查询max_page-1是否写满
            if(get_empty_log_index_form_page(buffer)==LOG_PER_PAGE){ // max_page-1写满了
                now_write_page = total_pages - 1; // max_page-1写满了，说明现在应该写入max_page
            }
            
        }
    }else{
        esp_partition_read(blackbox_partition, (now_write_page - 1) * PAGE_SIZE, buffer, PAGE_SIZE);
        uint8_t index = get_empty_log_index_form_page(buffer);
        if(index != LOG_PER_PAGE){ //上个页面没有写满
            now_write_page = now_write_page - 1;// 现在应该写入上个页面
            log_page_index = index;
        }else{
            log_page_index = 0;
        }
    }
    
    //计算之前已经写入的日志数量
    esp_partition_read(blackbox_partition, ((now_write_page+2)%total_pages) * PAGE_SIZE, buffer, PAGE_SIZE);
    if(memcmp(buffer, page_baffer, PAGE_SIZE)){//N+2页不是空的，说明循环写满了，N+2页是最新的一页
        uint32_t max_log_count = blackbox_partition->size / BLACKBOX_DATA_SIZE;
        log_count = max_log_count - 2*LOG_PER_PAGE + log_page_index;
    }else{//一次都没有写满从0到当前位置直接计算
        log_count = now_write_page * LOG_PER_PAGE + log_page_index;
    }

    // 读取当前页面数据到缓冲区
    esp_partition_read(blackbox_partition, now_write_page * PAGE_SIZE, buffer, PAGE_SIZE);
    memcpy(page_baffer, buffer, PAGE_SIZE);

    return ESP_OK;
}
static esp_err_t write_log_to_flash(BlackBoxData_t data){
    //写入页缓冲区
    size_t offset = log_page_index * BLACKBOX_DATA_SIZE;
    memcpy(page_baffer + offset, &data, sizeof(BlackBoxData_t));
    log_page_index = (log_page_index + 1) % (PAGE_SIZE / BLACKBOX_DATA_SIZE);
    log_count++;
    
    //写入物理页
    if (write_page(now_write_page, page_baffer) != ESP_OK) {
        ESP_LOGE("BlackBox", "Failed to write page %d", now_write_page);
        return ESP_ERR_INVALID_STATE;
    }
    
    //页面已满时切换下一页并擦除后续扇区
    if (log_page_index == 0) {
        now_write_page = (now_write_page + 1) % total_pages;
        memset(page_baffer, 0xFF, PAGE_SIZE);
        
        if (now_write_page % PAGES_PER_SECTOR == 0) {
            uint32_t sector_index = now_write_page / PAGES_PER_SECTOR;
            if (erase_sector((sector_index + 1) % total_sectors) != ESP_OK) {
            if (erase_sector((sector_index + 1) % total_sectors) != ESP_OK) {
                ESP_LOGE("BlackBox", "Failed to erase sector %d", sector_index + 1);
                return ESP_ERR_INVALID_STATE;
            }
        }
    }
    return ESP_OK;
}


esp_err_t BlackBox::add_log(const char *fmt, ...) {
    if(!log_enable){
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    // 1. 格式化字符串到临时缓冲区
    char log_str[LOG_STRING_MAX_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(log_str, sizeof(log_str), fmt, args);
    va_end(args);
    
    // 2. 构造黑匣子数据块（原直接字符串版本的逻辑）
    BlackBoxData_t data;
    data.global_state = global_state_blackbox;
    strncpy((char*)data.strlog, log_str, sizeof(data.strlog) - 1);
    data.strlog[sizeof(data.strlog) - 1] = '\0';
    data.timestamp = esp_timer_get_time() / 1000;
    data.crc_checksum = CRC8_Calc((uint8_t*)&data, sizeof(BlackBoxData_t) - 1);
    
    return write_log_to_flash(data);
}

void BlackBox::set_log_enable(bool enable){
    add_log("[BlackBox] : %s", enable ? "enabled" : "disabled");
    log_enable = enable;
}

uint32_t BlackBox::get_count(){
    return log_count;
}

BlackBoxData_t BlackBox::get_log(uint32_t index){
    BlackBoxData_t log_entry;  // 用于返回的静态对象
    if(index>=get_count()){
        ESP_LOGW("BlackBox", "Log Index out of range!");
        return log_entry;
    }
    uint8_t buffer[PAGE_SIZE];

    // 1. 确定最新日志的位置
    uint32_t last_page;
    uint8_t last_offset;

    if (log_page_index == 0) {
        // 当前页未写入任何新日志，最新日志在上一页的最后一个槽位
        last_page = (now_write_page == 0) ? (total_pages - 1) : (now_write_page - 1);
        last_offset = LOG_PER_PAGE - 1;
    } else {
        // 最新日志在当前页的 log_page_index - 1 位置
        last_page = now_write_page;
        last_offset = log_page_index - 1;
    }

    // 2. 根据 index 倒退找到目标日志的位置
    uint32_t target_page = last_page;
    uint8_t target_offset = last_offset;
    uint32_t remaining = index;

    while (remaining > 0) {
        if (target_offset >= remaining) {
            // 目标在同一页内
            target_offset -= remaining;
            remaining = 0;
        } else {
            // 需要跳到上一页
            remaining -= (target_offset + 1);
            // 上一页
            if (target_page == 0) {
                target_page = total_pages - 1;
            } else {
                target_page--;
            }
            target_offset = LOG_PER_PAGE - 1;  // 从上一页最后一个开始
        }
    }

    // 3. 读取目标页数据
    esp_err_t err = esp_partition_read(blackbox_partition,
                                       target_page * PAGE_SIZE,
                                       buffer,
                                       PAGE_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE("BlackBox", "Failed to read page %d", target_page);
        memset(static_cast<void*>(&log_entry), 0, sizeof(log_entry));
        return log_entry;
    }

    // 4. 提取对应日志
    memcpy(static_cast<void*>(&log_entry),
           buffer + target_offset * BLACKBOX_DATA_SIZE,
           sizeof(log_entry));

    // 可选：验证帧头，若不正确可返回空数据
    if (log_entry.sof != DATA_SOF) {
        ESP_LOGW("BlackBox", "Log at index %d has invalid SOF", index);
        // 不清零，直接返回，由调用者处理
    }

    return log_entry;
}