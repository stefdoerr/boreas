#pragma once
#include <vector>
#include <algorithm>

namespace boreas {

// Mono ring. Records continuously; copyWindow() extracts a stable snapshot.
// Assumes (window length + lookback) < size — holds for any window < 1 s.
class CircularBuffer {
public:
    void init(int capacity) {
        buf_.assign((size_t)capacity, 0.0f);
        size_ = capacity;
        writePos_ = 0;
    }
    void clear() {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        writePos_ = 0;
    }
    int size() const { return size_; }

    void write(float x) {
        buf_[(size_t)writePos_] = x;
        if (++writePos_ >= size_) writePos_ = 0;
    }

    // Fill dest[0..length-1] (chronological order) with the window ending
    // `lookback` samples behind the most recent write.
    void copyWindow(float* dest, int length, int lookback) const {
        if (lookback < 0) lookback = 0;
        if (length + lookback > size_) lookback = size_ - length;  // keep window inside the ring
        const int end   = writePos_ - 1 - lookback;   // index of dest[length-1]
        const int start = end - (length - 1);         // index of dest[0]
        for (int i = 0; i < length; ++i) {
            int idx = (start + i) % size_;
            if (idx < 0) idx += size_;
            dest[i] = buf_[(size_t)idx];
        }
    }

private:
    std::vector<float> buf_;
    int size_ = 0;
    int writePos_ = 0;
};

} // namespace boreas
