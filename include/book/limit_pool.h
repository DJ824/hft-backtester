#pragma once
#include "limit.h"
#include <memory>
#include <vector>

class LimitPool {
private:
  std::vector<std::unique_ptr<Limit>> pool_;
  std::vector<Limit*> available_;

public:
  inline explicit LimitPool(std::size_t initial_size = 1024) {
    pool_.reserve(initial_size);
    available_.reserve(initial_size);
    for (std::size_t i = 0; i < initial_size; ++i) {
      pool_.push_back(std::make_unique<Limit>(0));
      available_.push_back(pool_.back().get());
    }
  }

  __attribute__((always_inline))
  inline Limit* acquire(int32_t price, bool side) {
    Limit* l;
    if (available_.empty()) {
      pool_.push_back(std::make_unique<Limit>(price));
      l = pool_.back().get();
    } else {
      l = available_.back();
      available_.pop_back();
      l->reset();
      l->price_ = price;
    }
    l->side_ = side;
    return l;
  }

  __attribute__((always_inline))
  inline void release(Limit* l) {
    if (l) {
      l->reset();
      available_.push_back(l);
    }
  }

  __attribute__((always_inline))
  inline void reset(std::size_t reserve_hint = 1024) {
    available_.clear();
    available_.reserve(reserve_hint);

    // Return all limits to available pool
    for (auto& limit_ptr : pool_) {
      limit_ptr->reset();
      available_.push_back(limit_ptr.get());
    }
  }

  inline size_t size() const { return pool_.size(); }
  inline size_t available() const { return available_.size(); }
};