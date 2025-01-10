
/*
#include "limit_pool.h"

limit* limit_pool::acquire_limit(float price) {
    auto& limit_ptr = limits_by_price_[price];
    if (!limit_ptr) {
        limit_ptr = std::make_unique<limit>(price);
    }
    limit_ptr->set(price);
    return limit_ptr.get();
}

void limit_pool::release_limit(limit *curr) {
    auto it = limits_by_price_.find(curr->price_);
    if (it != limits_by_price_.end()) {
        it->second->reset();
    }
}


*/