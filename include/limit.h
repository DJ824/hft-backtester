
#ifndef DATABENTO_ORDERBOOK_LIMIT_H
#define DATABENTO_ORDERBOOK_LIMIT_H

#include <cstdint>
#include "order.h"

class Limit {
public:
    explicit Limit(Order* new_order);
    Limit(int32_t price);

    int32_t price_;
    uint64_t volume_;
    uint32_t num_orders_;
    Order* head_;
    Order* tail_;
    uint64_t total_volume() { return volume_; }
    bool side_;

    inline void add_order(Order* new_order) {
        if (head_ == nullptr && tail_ == nullptr) {
            head_ = new_order;
            tail_ = new_order;
        } else {
            tail_->next_ = new_order;
            new_order->prev_ = tail_;
            tail_ = tail_->next_;
            tail_->next_ = nullptr;
        }
        volume_ += new_order->size;
        ++num_orders_;
        new_order->parent_ = this;
    }

    inline void remove_order(Order* target) {
        if (!target || !head_) {
            return;
        }

        volume_ -= target->size;
        --num_orders_;

        if (head_ == tail_ && head_ == target) {
            head_ = nullptr;
            tail_ = nullptr;
        }
        else if (head_ == target) {
            head_ = head_->next_;
            if (head_) {
                head_->prev_ = nullptr;
            }
        }
        else if (tail_ == target) {
            tail_ = tail_->prev_;
            if (tail_) {
                tail_->next_ = nullptr;
            }
        }
        else {
            if (target->prev_) {
                target->prev_->next_ = target->next_;
            }
            if (target->next_) {
                target->next_->prev_ = target->prev_;
            }
        }

        target->next_ = nullptr;
        target->prev_ = nullptr;
        target->parent_ = nullptr;
    }

    int32_t get_price();
    uint64_t get_volume();
    uint32_t get_size();
    bool is_empty();
    void reset();
    void set(int32_t price);
};


#endif //DATABENTO_ORDERBOOK_LIMIT_H
