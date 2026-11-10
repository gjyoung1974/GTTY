// Single-producer / single-consumer lock-free ring buffer for audio samples.
#pragma once
#include <atomic>
#include <cstddef>
#include <vector>

namespace gtty {

template <typename T>
class SpscRing {
 public:
  explicit SpscRing(size_t capacityPow2 = 1 << 16) : buf_(capacityPow2), mask_(capacityPow2 - 1) {
    static_assert(std::atomic<size_t>::is_always_lock_free);
  }

  size_t capacity() const { return buf_.size(); }
  size_t size() const {
    return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire);
  }
  size_t space() const { return capacity() - size(); }

  // Returns the number of items actually written.
  size_t push(const T* data, size_t n) {
    size_t head = head_.load(std::memory_order_acquire);
    size_t tail = tail_.load(std::memory_order_relaxed);
    size_t avail = capacity() - (tail - head);
    if (n > avail) n = avail;
    for (size_t i = 0; i < n; ++i) buf_[(tail + i) & mask_] = data[i];
    tail_.store(tail + n, std::memory_order_release);
    return n;
  }

  // Returns the number of items actually read.
  size_t pop(T* out, size_t n) {
    size_t tail = tail_.load(std::memory_order_acquire);
    size_t head = head_.load(std::memory_order_relaxed);
    size_t avail = tail - head;
    if (n > avail) n = avail;
    for (size_t i = 0; i < n; ++i) out[i] = buf_[(head + i) & mask_];
    head_.store(head + n, std::memory_order_release);
    return n;
  }

  void clear() { head_.store(tail_.load(std::memory_order_acquire), std::memory_order_release); }

 private:
  std::vector<T> buf_;
  size_t mask_;
  alignas(64) std::atomic<size_t> head_{0};
  alignas(64) std::atomic<size_t> tail_{0};
};

}  // namespace gtty
