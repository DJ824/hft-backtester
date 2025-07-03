#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <atomic>
#include <cassert>


struct alignas(64) Limit {
  int32_t price_ = 0;
  int32_t volume_ = 0;

  Order *head_ = nullptr;
  Order *tail_ = nullptr;

  bool side_ = false;

  [[nodiscard]] inline bool is_empty() const noexcept {
    return head_ == nullptr;
  }

  inline void add_order(Order *new_order) noexcept {
    new_order->prev_ = tail_;
    new_order->next_ = nullptr;
    if (tail_) {
      tail_->next_ = new_order;
    }
    else {
      head_ = new_order;
    }

    tail_ = new_order;
    volume_ += new_order->size;
  }

  inline void remove_order(Order *target) noexcept {
    if (target->prev_) {
      target->prev_->next_ = target->next_;
    } else {
      head_ = target->next_;
    }
    if (target->next_) {
      target->next_->prev_ = target->prev_;
    } else {
      tail_ = target->prev_;
    }
    volume_ -= target->size;
  }

private:
  static constexpr std::size_t used = 4  + 4 + 2 * sizeof(Order *) + 1 ;
  static constexpr std::size_t PAD = 64 - used;
  std::byte _pad[PAD]{};
};

static_assert(sizeof(Limit) == 64, "Limit must be 64 bytes");
static_assert(alignof(Limit) == 64, "Limit must be 64-byte aligned");