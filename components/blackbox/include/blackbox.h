#ifndef BLACKBOX_H
#define BLACKBOX_H
#include <stdint.h>

namespace BlackBox {
    constexpr uint32_t BLACKBOX_DATA_SIZE = 64;
    constexpr uint32_t PAGE_SIZE = 256; // 每页256字节
    constexpr uint32_t SECTOR_SIZE = 4096; // 每扇区4096字节
    constexpr uint32_t PAGES_PER_SECTOR = SECTOR_SIZE / PAGE_SIZE; // 每个扇区包含的页面数
    constexpr uint8_t LOG_PER_PAGE = PAGE_SIZE / BLACKBOX_DATA_SIZE; // 每页可以存储的日志条数
    constexpr uint8_t DATA_SOF = 0xAA; // 帧头，固定为0xAA
    
    #pragma pack(1)
    struct BlackBoxData_t {
        uint8_t sof = DATA_SOF; // 帧头，固定为0xAA
        uint32_t timestamp;
        float voltage;
        float current;
        float ah;
        float wh;
        uint32_t flags; //各种标志位,用于记录异常情况
        uint8_t strlog[38];
        uint8_t crc_checksum;
    }; // BlackBoxData_t 64 bytes
    static_assert(sizeof(BlackBoxData_t) == BLACKBOX_DATA_SIZE, "BlackBoxData_t size must be 64 bytes");
    static_assert(PAGE_SIZE%BLACKBOX_DATA_SIZE == 0, "PAGE_SIZE must be a multiple of BLACKBOX_DATA_SIZE");
    /**
     * @brief : 初始化黑匣子
     * @return  {*}
     */
    void init();

    /**
     * @brief : 添加一条日志
     * @return  {*}
     * @param {BlackBoxData_t*} data 不需要先计算crc校验码，函数内部会自动计算并添加
     */
    void add_log(BlackBoxData_t& data);

    /**
     * @brief : 获取日志总数
     * @return  {uint32_t} - 日志总数
     */
    uint32_t get_count();

    /**
     * @brief : 获取指定索引的日志
     * @note : 连续读取前需要先调用set_log_enable(false)禁用日志记录,读取完成后再调用set_log_enable(true)启用日志记录
     * @return  {BlackBoxData_t} - 指定索引的日志
     * @param {uint32_t} index - 日志索引,从0开始倒叙读取,0为最新日志
     */
    BlackBoxData_t get_log(uint32_t index);

     /**
     * @brief : 设置是否启用日志记录,用于在读取时防止新日志写入
     * @return  {*}
     * @param {bool} enable - 是否启用日志记录
     */
    void set_log_enable(bool enable);
}


#endif // BLACKBOX_H