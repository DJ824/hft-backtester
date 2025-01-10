
#include "limit.h"

Limit::Limit(int32_t price) {
    volume_ = 0;
    num_orders_ = 0;
    price_ = price;
    head_ = nullptr;
    tail_ = nullptr;
}

Limit::Limit(Order* new_order) :
        price_(new_order->price_),
        volume_(new_order->size),
        num_orders_(1),
        head_(new_order),
        tail_(new_order) {};



void Limit::set(int32_t price) { this->price_ = price; }

void Limit::reset() {
    this->price_ = 0;
    this->volume_ = 0;
    this->num_orders_ = 0;
    this->head_ = nullptr;
    this->tail_ = nullptr;
}


bool Limit::is_empty() { return head_ == nullptr && tail_ == nullptr; }



