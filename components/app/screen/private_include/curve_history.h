/*
 * @Description: 曲线页历史采样缓存与像素桶聚合接口
 */
#ifndef CURVE_HISTORY_H
#define CURVE_HISTORY_H

#include <cstddef>
#include <cstdint>

namespace SCREEN {

/**
 * @brief 曲线指标类型
 */
enum class CurveMetric : uint8_t {
    Voltage,
    Current,
    Power,
    Count,
};

/**
 * @brief 单个横向像素桶的统计结果
 */
struct CurveBucket {
    float minimum = 0.0f; /**< 桶内最小值 */
    float maximum = 0.0f; /**< 桶内最大值 */
    float average = 0.0f; /**< 桶内平均值 */
    bool valid = false;   /**< 桶内是否包含有效采样 */
};

/**
 * @brief 曲线历史数据管理器
 *
 * 使用固定容量环形缓存持续保存电压和电流原始工程单位，并在绘制时按像素宽度
 * 聚合为最小值、最大值和平均值。功率由同一批测量快照实时计算，不重复占用缓存。
 */
class CurveHistory {
public:
    static constexpr uint32_t SAMPLE_INTERVAL_MS = 500;
    static constexpr uint32_t MAX_WINDOW_MS = 10 * 60 * 1000;
    static constexpr size_t MAX_SAMPLES = MAX_WINDOW_MS / SAMPLE_INTERVAL_MS;

    /**
     * @brief 获取曲线历史管理器单例
     * @return 曲线历史管理器引用
     */
    static CurveHistory& instance();

    /**
     * @brief 按固定周期采集一次当前全局测量值
     * @param now_ms 当前系统时间，单位 ms
     */
    void poll(uint32_t now_ms);

    /**
     * @brief 将指定时间窗口聚合为固定数量的横向像素桶
     * @param metric 指标类型
     * @param window_ms 时间窗口，单位 ms
     * @param buckets 输出像素桶数组
     * @param bucket_count 输出像素桶数量
     * @return 实际包含有效数据的像素桶数量
     */
    size_t build_buckets(CurveMetric metric, uint32_t window_ms,
                         CurveBucket* buckets, size_t bucket_count) const;

    /**
     * @brief 获取当前缓存中的采样数量
     * @return 当前采样数量
     */
    size_t sample_count() const;

private:
    struct Sample {
        uint16_t voltage_mV; /**< 电压，单位 1mV/LSB。 */
        uint16_t current_mA; /**< 电流绝对值，单位 1mA/LSB。 */
    };

    static_assert(sizeof(Sample) == 4, "CurveHistory::Sample must stay compact");

    CurveHistory() = default;

    /**
     * @brief 将缓存样本转换为指定指标的显示值
     * @param sample 历史样本
     * @param metric 指标类型
     * @return 显示值，单位分别为 V、A 或 W
     */
    static float metric_value(const Sample& sample, CurveMetric metric);

    Sample samples_[MAX_SAMPLES] = {};
    size_t write_index_ = 0;
    size_t count_ = 0;
    uint32_t last_sample_ms_ = 0;
    bool started_ = false;
};

} // namespace SCREEN

#endif
