#include <iostream>
#include <iomanip>
#include <numeric>
#include "orderbook.h"

Orderbook::Orderbook(DatabaseManager& db_manager)
        : db_manager_(db_manager), order_pool_(1000000), bid_count_(0), ask_count_(0) {
    bids_.get_allocator().allocate(1000);
    offers_.get_allocator().allocate(1000);
    order_lookup_.reserve(1000000);
    limit_lookup_.reserve(2000);
    ct_ = 0;
    voi_history_.reserve(40000);
}

Orderbook::~Orderbook() {
    for (auto& pair : bids_) {
        delete pair.second;
    }
    bids_.clear();

    for (auto& pair : offers_) {
        delete pair.second;
    }
    offers_.clear();

    order_lookup_.clear();
    limit_lookup_.clear();

}


template<bool Side>
typename BookSide<Side>::MapType& Orderbook::get_book_side() {
    if constexpr (Side) {
        return bids_;
    } else {
        return offers_;
    }
}

template<bool Side>
Limit* Orderbook::get_or_insert_limit(int32_t price) {
    std::pair<int32_t, bool> key = std::make_pair(price, Side);
    auto it = limit_lookup_.find(key);
    if (it == limit_lookup_.end()) {
        auto* new_limit = new Limit(price);
        get_book_side<Side>()[price] = new_limit;
        new_limit->side_ = Side;
        limit_lookup_[key] = new_limit;
        return new_limit;
    } else {
        return it->second;
    }
}


template<bool Side>
void Orderbook::add_limit_order(uint64_t id, int32_t price, uint32_t size, uint64_t unix_time) {
    Order* new_order = order_pool_.get_order();
    new_order->id_ = id;
    new_order->price_ = price;
    new_order->size = size;
    new_order->side_ = Side;
    new_order->unix_time_ = unix_time;

    Limit* curr_limit = get_or_insert_limit<Side>(price);
    order_lookup_[id] = new_order;
    curr_limit->add_order(new_order);

    if constexpr (Side) {
        ++bid_count_;
    } else {
        ++ask_count_;
    }

    //update_vol<Side>(price, size, true);
}


template<bool Side>
void Orderbook::remove_order(uint64_t id, int32_t price, uint32_t size) {
    auto target = order_lookup_[id];
    auto curr_limit = target->parent_;
    order_lookup_.erase(id);
    curr_limit->remove_order(target);
    if (curr_limit->is_empty()) {
        get_book_side<Side>().erase(price);
        std::pair<int32_t, bool> key = std::make_pair(price, Side);
        limit_lookup_.erase(key);
        target->parent_ = nullptr;
    }

    if constexpr (Side) {
        --bid_count_;
    } else {
        --ask_count_;
    }

    //update_vol<Side>(price, size, false);
    order_pool_.return_order(target);
}


template<bool Side>
void Orderbook::modify_order(uint64_t id, int32_t new_price, uint32_t new_size, uint64_t unix_time) {
    if (order_lookup_.find(id) == order_lookup_.end()) {
        add_limit_order<Side>(id, new_price, new_size, unix_time);
        return;
    }

    Order* target = order_lookup_[id];
    auto prev_price = target->price_;
    auto prev_limit = target->parent_;
    auto prev_size = target->size;

    if (prev_price != new_price) {
        prev_limit->remove_order(target);
        if (prev_limit->is_empty()) {
            get_book_side<Side>().erase(prev_price);
            std::pair<int32_t, bool> key = std::make_pair(prev_price, Side);
            limit_lookup_.erase(key);
        }
        Limit* new_limit = get_or_insert_limit<Side>(new_price);
        target->size = new_size;
        target->price_ = new_price;
        target->unix_time_ = unix_time;
        new_limit->add_order(target);
    } else if (prev_size < new_size) {
        prev_limit->remove_order(target);
        target->size = new_size;
        target->unix_time_ = unix_time;
        prev_limit->add_order(target);
    } else {
        target->size = new_size;
        target->unix_time_ = unix_time;
    }

    //update_modify_vol<Side>(prev_price, new_price, prev_size, new_size);
}

template<bool Side>
void Orderbook::trade_order(uint64_t id, int32_t price, uint32_t size) {
    auto og_size = size;
    auto& opposite_side = get_book_side<!Side>();

    if (opposite_side.empty()) {
        return;
    }

    auto trade_limit = get_or_insert_limit<!Side>(price);
    auto it = trade_limit->head_;

    while (it != nullptr) {
        if (size == it->size) {
            it->filled_ = true;
            size = 0;
            break;
        } else if (size < it->size) {
            it->size -= size;
            get_volume<!Side>() -= size;
            size = 0;
            break;
        } else {
            size -= it->size;
            it->filled_ = true;
            if (size == 0) {
                break;
            }
            it = it->next_;
        }
    }
    calculate_vwap(price, og_size);

}

template<bool Side>
int32_t& Orderbook::get_volume()  {
    if constexpr (Side) {
        return bid_vol_;
    } else {
        return ask_vol_;
    }
}


template<bool Side>
void Orderbook::update_vol(int32_t price, int32_t size, bool is_add) {
    if (!update_possible) {
        return;
    }

    int32_t& vol = Side ? bid_vol_ : ask_vol_;
    int32_t best_price = Side ? get_best_bid_price() : get_best_ask_price();

    if (std::abs(price - best_price) <= 2000) {
        vol += is_add ? size : -size;
    }
}

template<bool Side>
void Orderbook::update_modify_vol(int32_t og_price, int32_t new_price, int32_t og_size, int32_t new_size) {
    if (!update_possible) {
        return;
    }

    int32_t best_price = Side ? get_best_bid_price() : get_best_ask_price();
    int32_t& vol = Side ? bid_vol_ : ask_vol_;

    int32_t og_in_range = (std::abs(og_price - best_price) <= 2000);
    int32_t new_in_range = (std::abs(new_price - best_price) <= 2000);

    vol -= og_size * og_in_range;
    vol += new_size * new_in_range;
}

std::string Orderbook::get_formatted_time_fast() const {
    static thread_local char buffer[32];
    static thread_local time_t last_second = 0;
    static thread_local char last_second_str[20];

    auto now = std::chrono::system_clock::to_time_t(current_message_time_);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_message_time_.time_since_epoch()) % 1000;

    if (now != last_second) {
        last_second = now;
        struct tm tm_buf;
        localtime_r(&now, &tm_buf);
        strftime(last_second_str, sizeof(last_second_str), "%Y-%m-%d %H:%M:%S", &tm_buf);
    }

    snprintf(buffer, sizeof(buffer), "%s.%03d", last_second_str, static_cast<int>(ms.count()));
    return std::string(buffer);
}



void Orderbook::calculate_skew() {
    skew_ = log10(get_bid_depth()) - log10(get_ask_depth());
}


void Orderbook::calculate_imbalance() {
    uint64_t total_vol = bid_vol_ + ask_vol_;
    if (total_vol == 0) {
        imbalance_ = 0.0;
        return;
    }
    imbalance_ = static_cast<double>(static_cast<int64_t>(bid_vol_) - static_cast<int64_t>(ask_vol_)) / static_cast<double>(total_vol);
}

void Orderbook::reset() {
    for (auto &pair: bids_) {
        delete pair.second;
    }
    bids_.clear();

    for (auto &pair: offers_) {
        delete pair.second;
    }
    offers_.clear();

    order_lookup_.clear();
    limit_lookup_.clear();

    order_pool_.reset();

    bid_count_ = 0;
    ask_count_ = 0;
    update_possible = false;
    vwap_ = 0.0;
    sum1_ = 0.0;
    sum2_ = 0.0;
    skew_ = 0.0;
    bid_depth_ = 0.0;
    ask_depth_ = 0.0;
    bid_vol_ = 0;
    ask_vol_ = 0;
    last_reset_time_ = "";
    imbalance_ = 0.0;

    current_message_time_ = std::chrono::system_clock::time_point();

    bids_.get_allocator().allocate(1000);
    offers_.get_allocator().allocate(1000);
    order_lookup_.reserve(1000000);
    limit_lookup_.reserve(2000);
}

int32_t Orderbook::get_best_bid_price() const { return bids_.begin()->first; }

int32_t Orderbook::get_best_ask_price() const { return offers_.begin()->first; }

uint64_t Orderbook::get_count() const { return bid_count_ + ask_count_; }

uint64_t Orderbook::get_bid_depth() const { return bids_.begin()->second->volume_; }

uint64_t Orderbook::get_ask_depth() const { return offers_.begin()->second->volume_; }

template void Orderbook::trade_order<true>(uint64_t, int32_t, uint32_t);
template void Orderbook::trade_order<false>(uint64_t, int32_t, uint32_t);
template void Orderbook::modify_order<true>(uint64_t, int32_t, uint32_t, uint64_t);
template void Orderbook::modify_order<false>(uint64_t, int32_t, uint32_t, uint64_t);
template void Orderbook::remove_order<true>(uint64_t, int32_t, uint32_t);
template void Orderbook::remove_order<false>(uint64_t, int32_t, uint32_t);
template void Orderbook::add_limit_order<true>(uint64_t, int32_t, uint32_t, uint64_t);
template void Orderbook::add_limit_order<false>(uint64_t, int32_t, uint32_t, uint64_t);

template typename BookSide<true>::MapType& Orderbook::get_book_side<true>();
template typename BookSide<false>::MapType& Orderbook::get_book_side<false>();

template Limit* Orderbook::get_or_insert_limit<true>(int32_t);
template Limit* Orderbook::get_or_insert_limit<false>(int32_t);

template void Orderbook::update_vol<true>(int32_t, int32_t, bool);
template void Orderbook::update_vol<false>(int32_t, int32_t, bool);

template void Orderbook::update_modify_vol<true>(int32_t, int32_t, int32_t, int32_t);
template void Orderbook::update_modify_vol<false>(int32_t, int32_t, int32_t, int32_t);

template int32_t& Orderbook::get_volume<true>() ;
template int32_t& Orderbook::get_volume<false>() ;