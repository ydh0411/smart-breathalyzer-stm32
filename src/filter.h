// Moving-average filter that trims min/max samples to reject outliers
#ifndef FILTER_H
#define FILTER_H

#include "config.h"
#include <array>
#include <climits>

class MovingTrimmedAverage {
public:
    void push(const int sample) {
        buffer_[index_] = sample;
        index_ = (index_ + 1) % FILTER_WINDOW;
        if (count_ < FILTER_WINDOW) ++count_;
    }

    int value() const {//滤波算法核心逻辑：计算当前缓冲区内的样本总和、最小值和最大值，并根据样本数量决定是否进行修剪平均计算，返回最终的滤波结果
        //时间复杂度和空间复杂度都是O(N）
        if (count_ == 0) return 0;

        int sum = 0, min_value = INT_MAX, max_value = INT_MIN;
        for (size_t i = 0; i < count_; ++i) {
            const int v = buffer_[i];
            sum += v;
            if (v < min_value) min_value = v;
            if (v > max_value) max_value = v;
        }
        if (count_ <= 2) return sum / static_cast<int>(count_);
        return (sum - min_value - max_value) / static_cast<int>(count_ - 2);
    }

private:
    std::array<int, FILTER_WINDOW> buffer_{};
    size_t index_ = 0, count_ = 0;
};

#endif
