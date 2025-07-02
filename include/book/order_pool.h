#pragma once
#include "order.h"
#include <memory>
#include <vector>

class OrderPool {
private:
  std::vector<std::unique_ptr<Order>> pool_;
  std::vector<Order*> available_orders_;

public:
  inline explicit OrderPool(size_t initial_size) {
    pool_.reserve(initial_size);
    available_orders_.reserve(initial_size);
    for (size_t i = 0; i < initial_size; ++i) {
      pool_.push_back(std::make_unique<Order>());
      available_orders_.push_back(pool_.back().get());
    }
  }

  __attribute__((always_inline))
  inline Order* get_order() {
    if (available_orders_.empty()) {
      pool_.push_back(std::make_unique<Order>());
      return pool_.back().get();
    }
    Order* order = available_orders_.back();
    available_orders_.pop_back();
    return order;
  }

  __attribute__((always_inline))
  inline void return_order(Order* order) {
    // Reset order state for reuse
    order->id_ = 0;
    order->price_ = 0;
    order->size = 0;
    order->side_ = true;
    order->unix_time_ = 0;
    order->next_ = nullptr;
    order->prev_ = nullptr;
    order->parent_ = nullptr;
    order->filled_ = false;

    available_orders_.push_back(order);
  }

  __attribute__((always_inline))
  inline void reset() {
    available_orders_.clear();
    available_orders_.reserve(pool_.capacity());

    // Return all orders to available pool
    for (auto& order_ptr : pool_) {
      available_orders_.push_back(order_ptr.get());
    }
  }

  inline size_t size() const { return pool_.size(); }
  inline size_t available() const { return available_orders_.size(); }
};