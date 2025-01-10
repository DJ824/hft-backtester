
#ifndef DATABENTO_ORDERBOOK_ORDER_POOL_H
#define DATABENTO_ORDERBOOK_ORDER_POOL_H
#include "order.h"
#include <vector>



class OrderPool {
private:
    std::vector<std::unique_ptr<Order>> pool_;
    std::vector<Order*> available_orders_;

public:
    explicit OrderPool(size_t);
    //~OrderPool();
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
    void return_order(Order* order) {
        available_orders_.push_back(order);

    }

    __attribute__((always_inline))
    inline void reset() {
        pool_.clear();
        pool_.reserve(1000000);
    }
};


#endif //DATABENTO_ORDERBOOK_ORDER_POOL_H
