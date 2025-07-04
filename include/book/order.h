#pragma once
#include <cstdint>
#include <iomanip>
#include <sstream>

class Limit;

class Order {
public:
  uint64_t id_;
  int32_t price_;
  uint32_t size;
  bool side_;
  uint64_t unix_time_;
  Order* next_;
  Order* prev_;
  Limit* parent_;
  bool filled_;

  inline Order(uint64_t id, int32_t price, uint32_t size, bool side,
        uint64_t unix_time)
      : id_(id), price_(price), size(size), side_(side), unix_time_(unix_time),
        next_(nullptr), prev_(nullptr), parent_(nullptr), filled_(false) {
  }

  inline Order()
      : id_(0), price_(0), size(0), side_(true), unix_time_(0), next_(nullptr),
        prev_(nullptr), parent_(nullptr), filled_(false) {
  }
};