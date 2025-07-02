#pragma once
#include <cstdint>

struct book_message {
  uint64_t id_;
  uint64_t time_;
  signed int size_;
  int32_t price_;
  char action_;
  bool side_;

  book_message(uint64_t id, uint64_t time, uint32_t size, int32_t price,
               char action, bool side) :
    id_(id), time_(time), size_(size), price_(price), action_(action),
    side_(side) {
  }
};

