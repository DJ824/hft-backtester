#pragma once
#include "order.cpp"
#include <vector>
#include <memory>

class OrderPool {
private:
    std::vector<std::unique_ptr<Order>> pool_;
    std::vector<Order*> available_orders_;

public:
    explicit OrderPool(size_t initial_size) {
        pool_.reserve(initial_size);
        available_orders_.reserve(initial_size);
        for (size_t i = 0; i < initial_size; ++i) {
            pool_.push_back(std::make_unique<Order>());
            available_orders_.push_back(pool_.back().get());
        }
    }

    // Get an order from the pool, allocate new if needed
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

    // Return an order back to the pool
    __attribute__((always_inline))
    void return_order(Order* order) {
        available_orders_.push_back(order);
    }

    // Reset the pool
    __attribute__((always_inline))
    inline void reset() {
        pool_.clear();
        pool_.reserve(1000000);  // Default capacity for HFT
    }
};