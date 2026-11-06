// Wait-free single-producer single-consumer ring. N must be a power of two.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace danger {

template <typename T, size_t N>
class Spsc {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");

public:
    bool push(const T& v) {
        const uint32_t w = write_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) == N) return false;
        items_[w & (N - 1)] = v;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& v) {
        const uint32_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;
        v = items_[r & (N - 1)];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }

private:
    T items_[N];
    std::atomic<uint32_t> write_{0};
    std::atomic<uint32_t> read_{0};
};

}  // namespace danger
