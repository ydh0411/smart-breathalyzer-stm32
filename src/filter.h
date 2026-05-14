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

    int value() const {
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
