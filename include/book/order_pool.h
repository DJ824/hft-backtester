#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstdlib>
#include <new>
#include "order.h"

class OrderPool {
  static constexpr std::size_t PAGE_SIZE = 4096;

  std::vector<std::byte *> pages_;
  Order *freelist_ = nullptr;
  std::byte *curr_ = nullptr;
  std::byte *end_ = nullptr;

  void alloc_page() {
    void *mem = aligned_alloc(64, PAGE_SIZE);
    if (!mem) {
      throw std::bad_alloc{};
    }
    pages_.push_back(static_cast<std::byte *>(mem));
    curr_ = static_cast<std::byte *>(mem);
    end_ = curr_ + PAGE_SIZE;
  }

public:
  OrderPool() = default;

  ~OrderPool() {
    for (auto p : pages_) {
      std::free(p);
    }

  }

  Order *get_order() {
    if (freelist_) {
      Order *o = freelist_;
      freelist_ = freelist_->next_;
      return o;
    }
    if (curr_ == end_) {
      alloc_page();
    }

    Order *o = reinterpret_cast<Order *>(curr_);
    curr_ += sizeof(Order);
    return o;
  }

  void return_order(Order *order) {
    order->next_ = freelist_;
    freelist_ = order;
  }

  OrderPool(const OrderPool &) = delete;
  OrderPool &operator=(const OrderPool &) = delete;
};