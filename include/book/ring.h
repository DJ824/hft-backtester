#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "order.h"

static Order *const TOMBSTONE =
    reinterpret_cast<Order *>(static_cast<std::uintptr_t>(1));

struct Ring {
  Order **buff_ = nullptr;
  uint32_t head_ = 0;
  uint32_t tail_ = 0;
  uint32_t mask_ = 0;
  uint32_t tomb_ = 0;

  static Ring *create(uint32_t cap = 8) {
    cap = cap < 8 ? 8 : 1u << (32 - __builtin_clz(cap - 1));
    auto *r = static_cast<Ring *>(aligned_alloc(64, sizeof(Ring)));
    r->buff_ = static_cast<Order **>(aligned_alloc(64, cap * sizeof(Order *)));
    std::memset(r->buff_, 0, cap * sizeof(Order *));
    r->mask_ = cap - 1;
    r->head_ = r->tail_ = r->tomb_ = 0;
    return r;
  }

  uint32_t size() const noexcept { return (tail_ - head_) & mask; }

  void push(Order *o) {
    uint32_t new_tail = (tail_ + 1) & mask_;
    if (new_tail == head_)
      grow();
    buff_[tail_] = o;
    o->queue_idx_ = tail_;
    tail_ = new_tail;
  }

  Order *pop() {
    while (head_ != tail_ && (buff_[head_] == TOMBSTONE || buff_[head_] == nullptr)) {
      if (buff_[head_] == TOMBSTONE)
        --tomb_;
      buff_[head_] = nullptr;
      head_ = (head_ + 1) & mask_;
    }
    if (head_ == tail_)
      return nullptr; // empty
    Order *target = buff_[head_];
    buff_[head_] = TOMBSTONE;
    ++tomb_;
    head_ = (head_ + 1) & mask_;
    return target;
  }

  void cancel(Order *target) {
    uint32_t idx = target->queue_idx_;
    if (buff_[idx] != TOMBSTONE) {
      buff_[idx] = TOMBSTONE;
      ++tomb_;
    }
    if (tomb_ > 32 && tomb_ * 8 > size())
      compact();
  }

private:
  void grow() {
    uint32_t old_cap = mask_ + 1;
    uint32_t new_cap = old_cap << 1;
    auto *new_buf = static_cast<Order **>(
      aligned_alloc(64, new_cap * sizeof(Order *)));

    uint32_t write = 0;
    for (uint32_t read = head_; read != tail_; read = (read + 1) & mask_) {
      new_buf[write] = buff_[read];
      if (new_buf[write] != TOMBSTONE && new_buf[write] != nullptr)
        new_buf[write]->queue_idx_ = write;
      ++write;
    }
    std::free(buff_);
    buff_ = new_buf;
    head_ = 0;
    tail_ = write;
    mask_ = new_cap - 1;
  }

  void compact() {
    uint32_t write = 0;
    for (uint32_t read = head_; read != tail_; read = (read + 1) & mask_) {
      Order *ord = buff_[read];
      if (ord == TOMBSTONE || ord == nullptr)
        continue;

      if (read != write) {
        buff_[write] = ord;
        ord->queue_idx_ = write;
      }
      write = (write + 1) & mask;
    }
    head_ = 0;
    tail_ = write;
    tomb_ = 0;
  }
};