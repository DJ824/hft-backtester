
#include "order_pool.h"

OrderPool::OrderPool(size_t initial_size) {
    pool_.reserve(initial_size);
    available_orders_.reserve(initial_size);
    for (size_t i = 0; i < initial_size; ++i) {
        pool_.push_back(std::make_unique<Order>());
        available_orders_.push_back(pool_.back().get());
    }
}


