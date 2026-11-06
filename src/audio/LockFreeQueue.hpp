#pragma once
#include <atomic>
#include <cstddef>
#include <memory>

namespace nocturne {

// Bounded lock-free multi-producer/multi-consumer queue (Vyukov). Push/pop never
// block, allocate, or take locks, so it is safe on the audio thread.
template <typename T, std::size_t N>
class LockFreeQueue {
  static_assert((N & (N - 1)) == 0, "capacity must be a power of two");

 public:
  LockFreeQueue() {
    for (std::size_t i = 0; i < N; ++i) cells_[i].seq.store(i, std::memory_order_relaxed);
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
  }

  bool push(const T& v) {
    Cell* cell;
    std::size_t pos = head_.load(std::memory_order_relaxed);
    for (;;) {
      cell = &cells_[pos & (N - 1)];
      const std::size_t seq = cell->seq.load(std::memory_order_acquire);
      const auto dif = static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(pos);
      if (dif == 0) {
        if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) break;
      } else if (dif < 0) {
        return false;  // full
      } else {
        pos = head_.load(std::memory_order_relaxed);
      }
    }
    cell->data = v;
    cell->seq.store(pos + 1, std::memory_order_release);
    return true;
  }

  bool pop(T& out) {
    Cell* cell;
    std::size_t pos = tail_.load(std::memory_order_relaxed);
    for (;;) {
      cell = &cells_[pos & (N - 1)];
      const std::size_t seq = cell->seq.load(std::memory_order_acquire);
      const auto dif = static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(pos + 1);
      if (dif == 0) {
        if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) break;
      } else if (dif < 0) {
        return false;  // empty
      } else {
        pos = tail_.load(std::memory_order_relaxed);
      }
    }
    out = cell->data;
    cell->seq.store(pos + N, std::memory_order_release);
    return true;
  }

 private:
  struct Cell {
    std::atomic<std::size_t> seq;
    T data;
  };
  Cell cells_[N];
  alignas(64) std::atomic<std::size_t> head_;
  alignas(64) std::atomic<std::size_t> tail_;
};

}  // namespace nocturne
