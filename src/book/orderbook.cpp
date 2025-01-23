#pragma once

#include <cstdint>
#include <map>
#include <unordered_map>
#include <boost/functional/hash.hpp>
#include <utility>
#include <arm_neon.h>
#include <chrono>
#include "order.cpp"
#include "limit.cpp"
#include "message.h"
#include "order_pool.cpp"
#include "../include/lookup_table/lookup_table.h"

template<bool Side>
struct BookSide {
};

template<>
struct BookSide<true> {
    using MapType = std::map<int32_t, Limit*, std::greater<>>;
};

template<>
struct BookSide<false> {
    using MapType = std::map<int32_t, Limit*, std::less<>>;
};

class Orderbook {
private:
    OrderPool order_pool_;
    std::unordered_map<std::pair<int32_t, bool>, Limit *, boost::hash<std::pair<int32_t, bool>>> limit_lookup_;
    uint64_t bid_count_;
    uint64_t ask_count_;
    static constexpr size_t BUFFER_SIZE = 40000;
    size_t write_index_ = 0;
    size_t size_ = 0;

public:
    int64_t ct_ = 0;
    BookSide<true>::MapType bids_;
    BookSide<false>::MapType offers_;
    OpenAddressTable<Order> order_lookup_;
    std::chrono::system_clock::time_point current_message_time_;

    std::vector<int32_t> mid_prices_;
    std::vector<int32_t> mid_prices_curr_;
    std::vector<int32_t> voi_history_;
    std::vector<int32_t> voi_history_curr_;

    bool update_possible = false;
    double vwap_, sum1_, sum2_;
    float skew_, bid_depth_, ask_depth_;
    int32_t bid_vol_, ask_vol_;
    std::string last_reset_time_;
    double imbalance_;

    // VOI calculation variables
    int32_t bid_delta_;
    int32_t ask_delta_;
    int32_t prev_best_bid_;
    int32_t prev_best_ask_;
    int32_t prev_best_bid_volume_;
    int32_t prev_best_ask_volume_;
    int32_t voi_;

    explicit Orderbook() : order_pool_(1000000), bid_count_(0), ask_count_(0) {
        bids_.get_allocator().allocate(1000);
        offers_.get_allocator().allocate(1000);
        order_lookup_.reserve(1000000);
        limit_lookup_.reserve(2000);
        ct_ = 0;
        voi_history_.reserve(40000);
    }

    ~Orderbook() {
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
    }

    template<bool Side>
    typename BookSide<Side>::MapType &get_book_side() {
        if constexpr (Side) {
            return bids_;
        } else {
            return offers_;
        }
    }

    template<bool Side>
    Limit *get_or_insert_limit(int32_t price) {
        std::pair<int32_t, bool> key = std::make_pair(price, Side);
        auto it = limit_lookup_.find(key);
        if (it == limit_lookup_.end()) {
            auto *new_limit = new Limit(price);
            get_book_side<Side>()[price] = new_limit;
            new_limit->side_ = Side;
            limit_lookup_[key] = new_limit;
            return new_limit;
        } else {
            return it->second;
        }
    }

    template<bool Side>
    void add_limit_order(uint64_t id, int32_t price, uint32_t size, uint64_t unix_time) {
        Order *new_order = order_pool_.get_order();
        new_order->id_ = id;
        new_order->price_ = price;
        new_order->size = size;
        new_order->side_ = Side;
        new_order->unix_time_ = unix_time;

        Limit *curr_limit = get_or_insert_limit<Side>(price);
        order_lookup_.insert(id, new_order);
        curr_limit->add_order(new_order);

        if constexpr (Side) {
            ++bid_count_;
        } else {
            ++ask_count_;
        }
    }

    template<bool Side>
    void remove_order(uint64_t id, int32_t price, uint32_t size) {
        auto target = *order_lookup_.find(id);
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

        order_pool_.return_order(target);
    }

    template<bool Side>
    void modify_order(uint64_t id, int32_t new_price, uint32_t new_size, uint64_t unix_time) {
        Order **target_ptr = order_lookup_.find(id);
        if (!target_ptr) {
            add_limit_order<Side>(id, new_price, new_size, unix_time);
            return;
        }

        auto target = *target_ptr;
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
            Limit *new_limit = get_or_insert_limit<Side>(new_price);
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
    }

    template<bool Side>
    void trade_order(uint64_t id, int32_t price, uint32_t size) {
        auto og_size = size;
        auto &opposite_side = get_book_side<!Side>();

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
                if (size == 0) break;
                it = it->next_;
            }
        }
        calculate_vwap(price, og_size);
    }

    std::string get_formatted_time_fast() const {
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
            strftime(last_second_str, sizeof(last_second_str),
                     "%Y-%m-%d %H:%M:%S", &tm_buf);
        }

        snprintf(buffer, sizeof(buffer), "%s.%03d",
                 last_second_str, static_cast<int>(ms.count()));
        return std::string(buffer);
    }

    inline void process_msg(const message &msg) {
        ++ct_;
        auto nanoseconds = std::chrono::nanoseconds(msg.time_);
        auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(nanoseconds);
        current_message_time_ = std::chrono::system_clock::time_point(microseconds);

        switch (msg.action_) {
            case 'A':
                msg.side_ ? add_limit_order<true>(msg.id_, msg.price_, msg.size_, msg.time_)
                          : add_limit_order<false>(msg.id_, msg.price_, msg.size_, msg.time_);
                break;
            case 'C':
                msg.side_ ? remove_order<true>(msg.id_, msg.price_, msg.size_)
                          : remove_order<false>(msg.id_, msg.price_, msg.size_);
                break;
            case 'M':
                msg.side_ ? modify_order<true>(msg.id_, msg.price_, msg.size_, msg.time_)
                          : modify_order<false>(msg.id_, msg.price_, msg.size_, msg.time_);
                break;
            case 'T':
                msg.side_ ? trade_order<true>(msg.id_, msg.price_, msg.size_)
                          : trade_order<false>(msg.id_, msg.price_, msg.size_);
                break;
        }
    }

    inline void calculate_voi() {
        int32_t bid_delta = get_best_bid_price() - prev_best_bid_;
        int32_t ask_delta = get_best_ask_price() - prev_best_ask_;

        int32_t bid_cv = 0;
        int32_t ask_cv = 0;

        if (bid_delta >= 0) {
            if (bid_delta == 0) {
                bid_cv = get_best_bid_volume() - prev_best_bid_volume_;
            } else {
                bid_cv = get_best_bid_volume();
            }
        }

        if (ask_delta <= 0) {
            if (ask_delta == 0) {
                ask_cv = get_best_ask_volume() - prev_best_ask_volume_;
            } else {
                ask_cv = get_best_ask_volume();
            }
        }

        voi_ = bid_cv - ask_cv;
        voi_history_.push_back(voi_);

        prev_best_bid_ = get_best_bid_price();
        prev_best_ask_ = get_best_ask_price();
        prev_best_bid_volume_ = get_best_bid_volume();
        prev_best_ask_volume_ = get_best_ask_volume();
    }

    inline void calculate_voi_curr() {
        int32_t bid_delta = get_best_bid_price() - prev_best_bid_;
        int32_t ask_delta = get_best_ask_price() - prev_best_ask_;

        int32_t bid_cv = 0;
        int32_t ask_cv = 0;

        if (bid_delta >= 0) {
            if (bid_delta == 0) {
                bid_cv = get_best_bid_volume() - prev_best_bid_volume_;
            } else {
                bid_cv = get_best_bid_volume();
            }
        }

        if (ask_delta <= 0) {
            if (ask_delta == 0) {
                ask_cv = get_best_ask_volume() - prev_best_ask_volume_;
            } else {
                ask_cv = get_best_ask_volume();
            }
        }

        voi_ = bid_cv - ask_cv;
        voi_history_curr_.push_back(voi_);

        prev_best_bid_ = get_best_bid_price();
        prev_best_ask_ = get_best_ask_price();
        prev_best_bid_volume_ = get_best_bid_volume();
        prev_best_ask_volume_ = get_best_ask_volume();
    }

    inline void calculate_vols() {
        uint32x4_t bid_vol_vec = vdupq_n_u32(0);
        uint32x4_t ask_vol_vec = vdupq_n_u32(0);

        auto bid_it = bids_.begin();
        auto bid_end = bids_.end();
        auto ask_it = offers_.begin();
        auto ask_end = offers_.end();

        for (int i = 0; i < 100 && (bid_it != bid_end || ask_it != ask_end); i += 4) {
            uint32_t bid_chunk[4] = {0, 0, 0, 0};
            uint32_t ask_chunk[4] = {0, 0, 0, 0};

            for (int j = 0; j < 4; ++j) {
                if (bid_it != bid_end) {
                    bid_chunk[j] = bid_it->second->volume_;
                    ++bid_it;
                }
                if (ask_it != ask_end) {
                    ask_chunk[j] = ask_it->second->volume_;
                    ++ask_it;
                }
            }

            bid_vol_vec = vaddq_u32(bid_vol_vec, vld1q_u32(bid_chunk));
            ask_vol_vec = vaddq_u32(ask_vol_vec, vld1q_u32(ask_chunk));
        }

        bid_vol_ = vaddvq_u32(bid_vol_vec);
        ask_vol_ = vaddvq_u32(ask_vol_vec);
        update_possible = true;
    }

    inline void calculate_vwap(int32_t price, int32_t size) {
        sum1_ += (double) (price * size);
        sum2_ += (double) size;
        vwap_ = sum1_ / sum2_;
    }

    void calculate_skew() {
        skew_ = log10(get_bid_depth()) - log10(get_ask_depth());
    }

    void calculate_imbalance() {
        uint64_t total_vol = bid_vol_ + ask_vol_;
        if (total_vol == 0) {
            imbalance_ = 0.0;
            return;
        }
        imbalance_ = static_cast<double>(static_cast<int64_t>(bid_vol_) -
                                         static_cast<int64_t>(ask_vol_)) / static_cast<double>(total_vol);
    }

    void reset() {
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

    inline int32_t get_best_bid_price() const {
        return bids_.begin()->first;
    }

    inline int32_t get_best_ask_price() const {
        return offers_.begin()->first;
    }

    inline int32_t get_best_bid_volume() {
        return bids_.begin()->second->volume_;
    }

    inline int32_t get_best_ask_volume() {
        return offers_.begin()->second->volume_;
    }

    uint64_t get_bid_depth() const {
        return bids_.begin()->second->volume_;
    }

    uint64_t get_ask_depth() const {
        return offers_.begin()->second->volume_;
    }

    uint64_t get_count() const {
        return bid_count_ + ask_count_;
    }

    inline int32_t get_mid_price() {
        return (get_best_bid_price() + get_best_ask_price()) / 2;
    }

    void add_mid_price() {
        mid_prices_.push_back(get_mid_price());
    }

    void add_mid_price_curr() {
        mid_prices_curr_.push_back(get_mid_price());
    }

    int32_t get_indexed_mid_price(size_t index) {
        size_t read_index = (write_index_ - 1 - index + BUFFER_SIZE) % BUFFER_SIZE;
        return mid_prices_[read_index];
    }

    int32_t get_indexed_voi(size_t index) {
        return voi_history_[index];
    }

    template<bool Side>
    int32_t &get_volume() {
        if constexpr (Side) {
            return bid_vol_;
        } else {
            return ask_vol_;
        }
    }

    template<bool Side>
    void update_vol(int32_t price, int32_t size, bool is_add) {
        if (!update_possible) {
            return;
        }

        int32_t &vol = Side ? bid_vol_ : ask_vol_;
        int32_t best_price = Side ? get_best_bid_price() : get_best_ask_price();

        if (std::abs(price - best_price) <= 2000) {
            vol += is_add ? size : -size;
        }
    }

    template<bool Side>
    void update_modify_vol(int32_t og_price, int32_t new_price, int32_t og_size, int32_t new_size) {
        if (!update_possible) {
            return;
        }

        int32_t best_price = Side ? get_best_bid_price() : get_best_ask_price();
        int32_t &vol = Side ? bid_vol_ : ask_vol_;

        int32_t og_in_range = (std::abs(og_price - best_price) <= 2000);
        int32_t new_in_range = (std::abs(new_price - best_price) <= 2000);

        vol -= og_size * og_in_range;
        vol += new_size * new_in_range;
    }

};