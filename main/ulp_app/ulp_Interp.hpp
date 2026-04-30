#ifndef ULP_INTERP_HPP
#define ULP_INTERP_HPP

#include <stdint.h>

/**
 * @description: ULP核心定点数非等间隔插值类，不依赖STL，适用于资源受限的ULP LP核心
 * @note 校准点支持运行期加载（如从NVS读取），构造时仅分配内存空间
 * @note 插值算法为分段线性插值，查找区间使用二分查找，时间复杂度O(logN)
 * @tparam {InputType} 输入值类型，应为定点数整型（如int32_t）
 * @tparam {OutputType} 输出值类型，应为定点数整型（如int32_t）
 * @tparam {int} N 校准点数量，编译期确定
 */
template<typename InputType, typename OutputType, int N>
class UlpNonEquidistantInterp {
public:
    enum class Monotonicity { INCREASING, DECREASING }; /**< 输入序列单调性 */

    struct Point { InputType x; OutputType y; };        /**< 校准点结构体 */

    /**
     * @description: 默认构造函数，仅分配内存空间，校准数据未加载
     * @note 调用interpolate前必须先调用load或set_point+finish_load加载校准数据
     */
    UlpNonEquidistantInterp() noexcept
        : inputs_{}, outputs_{}, monotonicity_(Monotonicity::INCREASING), loaded_(false) {}

    /**
     * @description: 从Point数组一次性加载校准点数据
     * @param {const Point (&)[N]} pts 校准点数组，需严格单调（递增或递减）
     * @note 加载完成后自动推导单调性，无需手动调用finish_load
     */
    void load(Point (&pts)[N]) noexcept {
        for (int i = 0; i < N; ++i) {
            inputs_[i] = pts[i].x;
            outputs_[i] = pts[i].y;
        }
        monotonicity_ = (inputs_[0] < inputs_[N - 1])
            ? Monotonicity::INCREASING : Monotonicity::DECREASING;
        loaded_ = true;
    }

    /**
     * @description: 从分离的输入/输出数组加载校准点数据
     * @param {const InputType*} xs 输入值数组，需严格单调
     * @param {const OutputType*} ys 对应的输出值数组
     * @note 加载完成后自动推导单调性，无需手动调用finish_load
     */
    void load(InputType* xs, OutputType* ys) noexcept {
        for (int i = 0; i < N; ++i) {
            inputs_[i] = xs[i];
            outputs_[i] = ys[i];
        }
        monotonicity_ = (inputs_[0] < inputs_[N - 1])
            ? Monotonicity::INCREASING : Monotonicity::DECREASING;
        loaded_ = true;
    }

    /**
     * @description: 逐点设置单个校准点，适用于逐个从NVS读取后填充的场景
     * @param {int} idx 校准点索引，范围[0, N)
     * @param {InputType} x 输入值
     * @param {OutputType} y 输出值
     * @note 所有点设置完成后必须调用finish_load()完成加载
     */
    void set_point(int idx, InputType x, OutputType y) noexcept {
        if (idx >= 0 && idx < N) {
            inputs_[idx] = x;
            outputs_[idx] = y;
        }
    }

    /**
     * @description: 完成逐点加载，推导单调性并标记为已加载
     * @note 调用此函数前需确保所有校准点已通过set_point设置完毕
     */
    void finish_load() noexcept {
        monotonicity_ = (inputs_[0] < inputs_[N - 1])
            ? Monotonicity::INCREASING : Monotonicity::DECREASING;
        loaded_ = true;
    }

    /** @brief 查询校准数据是否已加载 */
    bool is_loaded() const noexcept { return loaded_; }

    /**
     * @description: 分段线性插值，根据输入值计算输出值，负数输入按原点对称处理
     * @param {InputType} x 待插值的输入值
     * @return {OutputType} 插值结果，超出范围时返回边界值（截断处理），负数输入返回负数输出
     * @note 校准表仅存储正数校准点，负数输入取绝对值插值后取反，即 f(-x) = -f(x)
     * @note 算法：取绝对值 → 二分查找定位所在区间 → 线性插值 y = y1 + (y2-y1)*(x-x1)/(x2-x1) → 恢复符号
     */
    OutputType interpolate(InputType x) const noexcept {
        // 原点对称处理：负数输入取绝对值插值，结果取反
        bool negative = (x < 0);
        InputType ax = negative ? -x : x;

        // 截断边界处理（ax >= 0，校准点均为正数，只检查递增上界）
        if (ax >= inputs_[N - 1]) {
            OutputType boundary = outputs_[N - 1];
            return negative ? -boundary : boundary;
        }
        if (ax <= inputs_[0]) {
            OutputType boundary = outputs_[0];
            return negative ? -boundary : boundary;
        }

        // 二分查找确定ax所在区间[i, i+1]
        int i = binary_search(ax);

        InputType  x1 = inputs_[i];
        InputType  x2 = inputs_[i + 1];
        OutputType y1 = outputs_[i];
        OutputType y2 = outputs_[i + 1];

        // 分段线性插值
        OutputType result = y1 + (y2 - y1) * (ax - x1) / (x2 - x1);
        return negative ? -result : result;
    }

    /** @brief 获取有效输入范围的最小值（考虑原点对称，为负数最大值取反） */
    InputType getMinInput() const noexcept {
        InputType max_pos = monotonicity_ == Monotonicity::INCREASING ? inputs_[N - 1] : inputs_[0];
        return -max_pos;
    }

    /** @brief 获取有效输入范围的最大值（正数最大值） */
    InputType getMaxInput() const noexcept {
        return monotonicity_ == Monotonicity::INCREASING ? inputs_[N - 1] : inputs_[0];
    }

private:
    /**
     * @description: 二分查找，找到x所在的区间左端点索引i，使得inputs_[i] <= x <= inputs_[i+1]
     * @param {InputType} x 待查找的输入值
     * @return {int} 区间左端点索引，范围[0, N-2]
     */
    int binary_search(InputType x) const noexcept {
        int lo = 0, hi = N - 1;
        if (monotonicity_ == Monotonicity::INCREASING) {
            // 递增序列：查找首个 >= x 的位置
            while (lo < hi) {
                int mid = (lo + hi) / 2;
                if (inputs_[mid] < x)
                    lo = mid + 1;
                else
                    hi = mid;
            }
        } else {
            // 递减序列：查找首个 <= x 的位置
            while (lo < hi) {
                int mid = (lo + hi) / 2;
                if (inputs_[mid] > x)
                    lo = mid + 1;
                else
                    hi = mid;
            }
        }
        int i = lo - 1;
        if (i < 0) i = 0;
        if (i >= N - 1) i = N - 2;
        return i;
    }

    InputType  inputs_[N];                          /**< 校准点输入值数组 */
    OutputType outputs_[N];                         /**< 校准点输出值数组 */
    Monotonicity monotonicity_;                      /**< 输入序列单调性 */
    bool loaded_;                                    /**< 校准数据是否已加载 */
};

#endif
