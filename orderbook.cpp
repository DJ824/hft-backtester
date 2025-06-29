#pragma once

#include "../../include/lookup_table/lookup_table.h"
#include "../../include/message.h"
#include "limit.cpp"
#include "order.cpp"
#include "order_pool.cpp"
#include "limit_pool.cpp"
#include <boost/functional/hash.hpp>
#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <iostream>

template <bool Side>
struct BookSide
{
    using MapType = std::vector<std::pair<int32_t, Limit*>>;
    static constexpr auto compare = [](const std::pair<int32_t, Limit*>& a,
                                       int32_t b)
    {
        if constexpr (Side)
        {
            return a.first < b;
        }
        else
        {
            return a.first > b;
        }
    };
};

class Orderbook
{
private:
    OrderPool order_pool_;
    LimitPool limit_pool_;
    // std::unordered_map<std::pair<int32_t, bool>, Limit *,
    //                    boost::hash<std::pair<int32_t, bool>>>
    //     limit_lookup_;
    OpenAddressTable<uint64_t, Limit*> limit_lookup_;

    uint64_t bid_count_;
    uint64_t ask_count_;
    static constexpr ssize_t BUFFER_SIZE = 40000;
    static constexpr ssize_t INITIAL_LEVELS = 1000;
    static constexpr ssize_t INITIAL_ORDERS = 1000000;
    ssize_t write_index_ = 0;
    ssize_t size_ = 0;

public:
    int64_t ct_ = 0;
    BookSide<true>::MapType bids_;
    BookSide<false>::MapType offers_;
    OpenAddressTable<uint64_t, Order*> order_lookup_;
    std::chrono::system_clock::time_point current_message_time_;

    std::vector<int32_t> mid_prices_;
    std::vector<int32_t> mid_prices_curr_;
    std::vector<int32_t> voi_history_;
    std::vector<int32_t> voi_history_curr_;

    double vwap_ = 0.0;
    double sum1_ = 0.0;
    double sum2_ = 0.0;
    float skew_ = 0.0;
    float bid_depth_ = 0.0;
    float ask_depth_ = 0.0;
    int32_t bid_vol_ = 0;
    int32_t ask_vol_ = 0;
    double imbalance_ = 0.0;

    bool update_possible = false;

    int32_t bid_delta_ = 0;
    int32_t ask_delta_ = 0;
    int32_t prev_best_bid_ = 0;
    int32_t prev_best_ask_ = 0;
    int32_t prev_best_bid_volume_ = 0;
    int32_t prev_best_ask_volume_ = 0;
    int32_t voi_ = 0;

    explicit Orderbook() :
        order_pool_(INITIAL_ORDERS), bid_count_(0), ask_count_(0), limit_pool_(INITIAL_LEVELS * 2)
    {
        bids_.reserve(INITIAL_LEVELS);
        offers_.reserve(INITIAL_LEVELS);
        order_lookup_.reserve(INITIAL_ORDERS);
        limit_lookup_.reserve(2000);
        voi_history_.reserve(BUFFER_SIZE);
        mid_prices_.reserve(BUFFER_SIZE);
        voi_history_curr_.reserve(BUFFER_SIZE);
        mid_prices_curr_.reserve(BUFFER_SIZE);
    }

    ~Orderbook() { reset(); }

    template <bool Side>
    typename BookSide<Side>::MapType& get_book_side()
    {
        if constexpr (Side)
        {
            return bids_;
        }
        else
        {
            return offers_;
        }
    }

    template <bool Side>
    __attribute__((always_inline)) Limit* get_or_insert_limit(int32_t price)
    {
        //auto key = std::make_pair(price, Side);
        uint64_t key = (static_cast<uint64_t>(price) << 1) | (Side ? 1 : 0);
        auto it = limit_lookup_.find(key);

        if (it != nullptr)
        {
            return *it;
        }

        //auto *limit = new Limit(price);
        auto limit = limit_pool_.acquire(price, Side);
        limit->side_ = Side;

        auto& levels = get_book_side<Side>();
        auto level_it = std::lower_bound(levels.begin(), levels.end(), price,
                                         BookSide<Side>::compare);
        levels.insert(level_it, {price, limit});

        limit_lookup_.insert(key, limit);
        return limit;
    }

    template <bool Side>
    __attribute__((always_inline)) void
    add_limit_order(uint64_t id, int32_t price, uint32_t size,
                    uint64_t unix_time)
    {
        Order* new_order = order_pool_.get_order();
        new_order->id_ = id;
        new_order->price_ = price;
        new_order->size = size;
        new_order->side_ = Side;
        new_order->unix_time_ = unix_time;

        Limit* curr_limit = get_or_insert_limit<Side>(price);
        order_lookup_.insert(id, new_order);
        curr_limit->add_order(new_order);

        if constexpr (Side)
        {
            ++bid_count_;
        }
        else
        {
            ++ask_count_;
        }
    }

    template <bool Side>
    __attribute__((always_inline)) void remove_order(uint64_t id, int32_t price,
                                                     uint32_t size)
    {
        auto target = *order_lookup_.find(id);
        auto curr_limit = target->parent_;
        curr_limit->remove_order(target);

        if (curr_limit->is_empty())
        {
            auto& levels = get_book_side<Side>();
            auto it = std::lower_bound(levels.begin(), levels.end(), price,
                                       BookSide<Side>::compare);
            if (it != levels.end() && it->first == price)
            {
                levels.erase(it);
            }
            uint64_t key = (static_cast<uint64_t>(price) << 1) | (Side ? 1 : 0);
            //limit_lookup_.erase(std::make_pair(price, Side));
            limit_lookup_.erase(key);
            limit_pool_.release(curr_limit);
            //delete curr_limit;
        }

        if constexpr (Side)
        {
            --bid_count_;
        }
        else
        {
            --ask_count_;
        }

        order_lookup_.erase(id);
        order_pool_.return_order(target);
    }

    template <bool Side>
    __attribute__((always_inline)) void
    modify_order(uint64_t id, int32_t new_price, uint32_t new_size,
                 uint64_t unix_time)
    {
        Order** target_ptr = order_lookup_.find(id);
        if (!target_ptr)
        {
            add_limit_order<Side>(id, new_price, new_size, unix_time);
            return;
        }

        auto target = *target_ptr;
        auto prev_price = target->price_;
        auto prev_limit = target->parent_;

        if (prev_price != new_price)
        {
            remove_order<Side>(id, prev_price, target->size);
            add_limit_order<Side>(id, new_price, new_size, unix_time);
        }
        else
        {
            uint32_t old_size = target->size;
            target->size = new_size;
            if (new_size > old_size)
            {
                prev_limit->volume_ += (new_size - old_size);
            }
            else
            {
                prev_limit->volume_ -= (old_size - new_size);
            }
            target->unix_time_ = unix_time;
        }
    }

    // todo: replace with avx2 simd
    inline void calculate_vols()
    {
        bid_vol_ = 0;
        ask_vol_ = 0;

        if (!bids_.empty())
        {
            size_t count = 0;
            for (auto it = bids_.rbegin(); it != bids_.rend() && count < 40;
                 ++it, ++count)
            {
                bid_vol_ += it->second->volume_;
            }
        }

        if (!offers_.empty())
        {
            size_t count = 0;
            for (auto it = offers_.begin(); it != offers_.end() && count < 40;
                 ++it, ++count)
            {
                ask_vol_ += it->second->volume_;
            }
        }

        calculate_imbalance();
    }

    inline void process_msg(const book_message& msg)
    {
        ++ct_;
        auto nanoseconds = std::chrono::nanoseconds(msg.time_);
        auto microseconds =
            std::chrono::duration_cast<std::chrono::microseconds>(nanoseconds);
        current_message_time_ = std::chrono::system_clock::time_point(microseconds);

        switch (msg.action_)
        {
        case 'A':
            msg.side_
                ? add_limit_order<true>(msg.id_, msg.price_, msg.size_, msg.time_)
                : add_limit_order<false>(msg.id_, msg.price_, msg.size_, msg.time_);
            break;
        case 'C':
            msg.side_
                ? remove_order<true>(msg.id_, msg.price_, msg.size_)
                : remove_order<false>(msg.id_, msg.price_, msg.size_);
            break;
        case 'M':
            msg.side_
                ? modify_order<true>(msg.id_, msg.price_, msg.size_, msg.time_)
                : modify_order<false>(msg.id_, msg.price_, msg.size_, msg.time_);
            break;
        case 'T':

            break;
        }
    }

    inline void calculate_voi()
    {
        if (bids_.empty() || offers_.empty())
            return;

        int32_t bid_delta = bids_.back().first - prev_best_bid_;
        int32_t ask_delta = offers_.front().first - prev_best_ask_;

        int32_t bid_cv = 0;
        int32_t ask_cv = 0;

        if (bid_delta >= 0)
        {
            if (bid_delta == 0)
            {
                bid_cv = bids_.back().second->volume_ - prev_best_bid_volume_;
            }
            else
            {
                bid_cv = bids_.back().second->volume_;
            }
        }

        if (ask_delta <= 0)
        {
            if (ask_delta == 0)
            {
                ask_cv = offers_.front().second->volume_ - prev_best_ask_volume_;
            }
            else
            {
                ask_cv = offers_.front().second->volume_;
            }
        }

        voi_ = bid_cv - ask_cv;
        voi_history_.push_back(voi_);

        prev_best_bid_ = bids_.empty() ? 0 : bids_.back().first;
        prev_best_ask_ = offers_.empty() ? 0 : offers_.front().first;
        prev_best_bid_volume_ = bids_.empty() ? 0 : bids_.back().second->volume_;
        prev_best_ask_volume_ =
            offers_.empty() ? 0 : offers_.front().second->volume_;
    }

    inline void calculate_voi_curr()
    {
        if (bids_.empty() || offers_.empty())
            return;

        int32_t bid_delta = bids_.back().first - prev_best_bid_;
        int32_t ask_delta = offers_.front().first - prev_best_ask_;

        int32_t bid_cv = 0;
        int32_t ask_cv = 0;

        if (bid_delta >= 0)
        {
            if (bid_delta == 0)
            {
                bid_cv = bids_.back().second->volume_ - prev_best_bid_volume_;
            }
            else
            {
                bid_cv = bids_.back().second->volume_;
            }
        }

        if (ask_delta <= 0)
        {
            if (ask_delta == 0)
            {
                ask_cv = offers_.front().second->volume_ - prev_best_ask_volume_;
            }
            else
            {
                ask_cv = offers_.front().second->volume_;
            }
        }

        voi_ = bid_cv - ask_cv;
        voi_history_curr_.push_back(voi_);

        prev_best_bid_ = bids_.empty() ? 0 : bids_.back().first;
        prev_best_ask_ = offers_.empty() ? 0 : offers_.front().first;
        prev_best_bid_volume_ = bids_.empty() ? 0 : bids_.back().second->volume_;
        prev_best_ask_volume_ =
            offers_.empty() ? 0 : offers_.front().second->volume_;
    }

    inline void calculate_vwap(int32_t price, int32_t size)
    {
        sum1_ += static_cast<double>(price * size);
        sum2_ += static_cast<double>(size);
        vwap_ = sum1_ / sum2_;
    }

    void calculate_skew()
    {
        bid_depth_ = static_cast<float>(get_best_bid_volume());
        ask_depth_ = static_cast<float>(get_best_ask_volume());
        if (bid_depth_ > 0 && ask_depth_ > 0)
        {
            skew_ = log10f(bid_depth_) - log10f(ask_depth_);
        }
    }

    void calculate_imbalance()
    {
        uint64_t total_vol = bid_vol_ + ask_vol_;
        if (total_vol == 0)
        {
            imbalance_ = 0.0;
            return;
        }
        imbalance_ = static_cast<double>(static_cast<int64_t>(bid_vol_) -
                static_cast<int64_t>(ask_vol_)) /
            static_cast<double>(total_vol);
    }

    std::string get_formatted_time_fast() const
    {
        static thread_local char buffer[32];
        static thread_local time_t last_second = 0;
        static thread_local char last_second_str[20];

        auto now = std::chrono::system_clock::to_time_t(current_message_time_);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                current_message_time_.time_since_epoch()) %
            1000;

        if (now != last_second)
        {
            last_second = now;
            struct tm tm_buf;
            localtime_r(&now, &tm_buf);
            strftime(last_second_str, sizeof(last_second_str), "%Y-%m-%d %H:%M:%S",
                     &tm_buf);
        }

        snprintf(buffer, sizeof(buffer), "%s.%03d", last_second_str,
                 static_cast<int>(ms.count()));
        return std::string(buffer);
    }

    inline int32_t get_best_bid_price() const
    {
        return bids_.empty() ? 0 : bids_.back().first;
    }

    inline int32_t get_best_ask_price() const
    {
        return offers_.empty() ? 0 : offers_.back().first;
    }

    inline int32_t get_best_bid_volume() const
    {
        return bids_.empty() ? 0 : bids_.back().second->volume_;
    }

    inline int32_t get_best_ask_volume() const
    {
        return offers_.empty() ? 0 : offers_.back().second->volume_;
    }

    uint64_t get_bid_depth() const
    {
        return bids_.empty() ? 0 : bids_.back().second->volume_;
    }

    uint64_t get_ask_depth() const
    {
        return offers_.empty() ? 0 : offers_.front().second->volume_;
    }

    uint64_t get_count() const { return bid_count_ + ask_count_; }

    inline int32_t get_mid_price()
    {
        return (get_best_bid_price() + get_best_ask_price()) / 2;
    }

    void add_mid_price() { mid_prices_.push_back(get_mid_price()); }

    void add_mid_price_curr() { mid_prices_curr_.push_back(get_mid_price()); }

    int32_t get_indexed_mid_price(size_t index)
    {
        size_t read_index = (write_index_ - 1 - index + BUFFER_SIZE) % BUFFER_SIZE;
        return mid_prices_[read_index];
    }

    int32_t get_indexed_voi(size_t index) { return voi_history_[index]; }

    template <bool Side>
    int32_t& get_volume()
    {
        if constexpr (Side)
        {
            return bid_vol_;
        }
        else
        {
            return ask_vol_;
        }
    }

    void print_top_levels(int num_levels = 20) const
    {
        std::cout << std::setw(12) << "Ask Price"
            << std::setw(12) << "Ask Volume"
            << "  |  "
            << std::setw(12) << "Bid Price"
            << std::setw(12) << "Bid Volume" << std::endl;
        std::cout << std::string(60, '-') << std::endl;

        auto ask_it = offers_.rbegin();
        auto bid_it = bids_.rbegin();

        for (int i = 0; i < num_levels; ++i)
        {
            std::string ask_price = "--";
            std::string ask_volume = "--";
            if (ask_it != offers_.rend())
            {
                ask_price = std::to_string(ask_it->first);
                ask_volume = std::to_string(ask_it->second->volume_);
                ++ask_it;
            }

            std::string bid_price = "--";
            std::string bid_volume = "--";
            if (bid_it != bids_.rend())
            {
                bid_price = std::to_string(bid_it->first);
                bid_volume = std::to_string(bid_it->second->volume_);
                ++bid_it;
            }

            std::cout << std::setw(12) << ask_price
                << std::setw(12) << ask_volume
                << "  |  "
                << std::setw(12) << bid_price
                << std::setw(12) << bid_volume << std::endl;
        }
        std::cout << "========================================\n" << std::endl;
    }


    void reset()
    {
        bids_.clear();
        offers_.clear();
        order_lookup_.clear();
        limit_lookup_.clear();
        order_pool_.reset();
        limit_pool_.reset();
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
        imbalance_ = 0.0;
        voi_history_.clear();
        mid_prices_.clear();
        voi_history_curr_.clear();
        mid_prices_curr_.clear();
        bids_.reserve(INITIAL_LEVELS);
        offers_.reserve(INITIAL_LEVELS);
        order_lookup_.reserve(INITIAL_ORDERS);
        limit_lookup_.reserve(INITIAL_LEVELS);
    }
};
