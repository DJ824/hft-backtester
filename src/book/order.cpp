

#include "order.h"
#include <iomanip>
#include <sstream>

Order::Order(uint64_t id, int32_t price, uint32_t size, bool side, uint64_t unix_time)
        : id_(id), size(size), price_(price), side_(side), unix_time_(unix_time), filled_(false), next_(nullptr),
          prev_(nullptr), parent_(nullptr) {
}

Order::Order() : id_(0), size(0), price_(0), side_(true), unix_time_(0), filled_(false), next_(nullptr),
                 prev_(nullptr), parent_(nullptr) {}