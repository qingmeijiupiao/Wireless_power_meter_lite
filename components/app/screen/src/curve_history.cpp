#include "curve_history.h"

#include <algorithm>
#include <cmath>

#include "global_state.h"

namespace SCREEN {
namespace {

/**
 * @brief 将有符号微安值压缩为无符号毫安值。
 *
 * 曲线只展示电流绝对值，因此使用 1mA/LSB 保存，并对超出 uint16_t
 * 范围的数据做饱和处理，使每个历史采样点保持 4 字节。
 */
uint16_t compact_current_mA(int32_t current_uA) {
    const int64_t magnitude_uA = current_uA < 0
        ? -static_cast<int64_t>(current_uA)
        : static_cast<int64_t>(current_uA);
    const uint64_t rounded_mA =
        (static_cast<uint64_t>(magnitude_uA) + 500U) / 1000U;
    return static_cast<uint16_t>(
        std::min<uint64_t>(rounded_mA, UINT16_MAX));
}

} // namespace

CurveHistory& CurveHistory::instance() {
    static CurveHistory history;
    return history;
}

void CurveHistory::poll(uint32_t now_ms) {
    if (started_ && now_ms - last_sample_ms_ < SAMPLE_INTERVAL_MS) {
        return;
    }

    const GlobalMeasurementSnapshot measurement = get_global_measurement_snapshot();
    samples_[write_index_] = {
        .voltage_mV = measurement.voltage_mV,
        .current_mA = compact_current_mA(measurement.current_uA),
    };
    write_index_ = (write_index_ + 1) % MAX_SAMPLES;
    count_ = std::min(count_ + 1, MAX_SAMPLES);
    last_sample_ms_ = now_ms;
    started_ = true;
}

size_t CurveHistory::build_buckets(CurveMetric metric, uint32_t window_ms,
                                   CurveBucket* buckets, size_t bucket_count) const {
    if (buckets == nullptr || bucket_count == 0) {
        return 0;
    }

    for (size_t i = 0; i < bucket_count; ++i) {
        buckets[i] = {};
    }

    const size_t requested_samples = std::max<size_t>(
        1, std::min<size_t>(MAX_SAMPLES, window_ms / SAMPLE_INTERVAL_MS));
    const size_t visible_samples = std::min(count_, requested_samples);
    if (visible_samples == 0) {
        return 0;
    }

    const size_t oldest_index = (write_index_ + MAX_SAMPLES - visible_samples) % MAX_SAMPLES;

    if (visible_samples <= bucket_count) {
        // 短时间窗口的采样点少于横向像素时，对相邻样本做线性插值。
        // 这样切换到 10s 等窗口后曲线仍能覆盖整个绘图区，而不是只占右侧一部分。
        for (size_t bucket_index = 0; bucket_index < bucket_count; ++bucket_index) {
            float value = metric_value(samples_[oldest_index], metric);
            if (visible_samples > 1 && bucket_count > 1) {
                const float sample_position =
                    static_cast<float>(bucket_index) * static_cast<float>(visible_samples - 1) /
                    static_cast<float>(bucket_count - 1);
                const size_t left_offset = static_cast<size_t>(sample_position);
                const size_t right_offset = std::min(left_offset + 1, visible_samples - 1);
                const float fraction = sample_position - static_cast<float>(left_offset);
                const float left_value = metric_value(
                    samples_[(oldest_index + left_offset) % MAX_SAMPLES], metric);
                const float right_value = metric_value(
                    samples_[(oldest_index + right_offset) % MAX_SAMPLES], metric);
                value = left_value + (right_value - left_value) * fraction;
            }

            buckets[bucket_index] = {
                .minimum = value,
                .maximum = value,
                .average = value,
                .valid = true,
            };
        }
        return bucket_count;
    }

    size_t valid_buckets = 0;
    for (size_t bucket_index = 0; bucket_index < bucket_count; ++bucket_index) {
        const size_t sample_begin = bucket_index * visible_samples / bucket_count;
        const size_t sample_end = (bucket_index + 1) * visible_samples / bucket_count;
        CurveBucket& bucket = buckets[bucket_index];
        float sum = 0.0f;
        size_t samples_in_bucket = 0;

        for (size_t sample_offset = sample_begin; sample_offset < sample_end; ++sample_offset) {
            const Sample& sample = samples_[(oldest_index + sample_offset) % MAX_SAMPLES];
            const float value = metric_value(sample, metric);
            if (!bucket.valid) {
                bucket.minimum = value;
                bucket.maximum = value;
                bucket.valid = true;
            } else {
                bucket.minimum = std::min(bucket.minimum, value);
                bucket.maximum = std::max(bucket.maximum, value);
            }
            sum += value;
            samples_in_bucket++;
        }

        if (bucket.valid && samples_in_bucket > 0) {
            bucket.average = sum / samples_in_bucket;
            valid_buckets++;
        }
    }

    return valid_buckets;
}

size_t CurveHistory::sample_count() const {
    return count_;
}

float CurveHistory::metric_value(const Sample& sample, CurveMetric metric) {
    const float voltage = sample.voltage_mV / 1000.0f;
    const float current = sample.current_mA / 1000.0f;
    switch (metric) {
        case CurveMetric::Voltage:
            return voltage;
        case CurveMetric::Current:
            return current;
        case CurveMetric::Power:
            return voltage * current;
        default:
            return 0.0f;
    }
}

} // namespace SCREEN
