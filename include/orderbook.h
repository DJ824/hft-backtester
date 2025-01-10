#ifndef DATABENTO_ORDERBOOK_ORDERBOOK_H
#define DATABENTO_ORDERBOOK_ORDERBOOK_H

#include <cstdint>
#include <map>
#include <unordered_map>
#include <boost/functional/hash.hpp>
#include <utility>
#include <arm_neon.h>
#include <iterator>
#include "order.h"
#include "limit.h"
#include "limit_pool.h"
#include "order_pool.h"
#include "message.h"
#include "database.h"


template<bool Side>
struct BookSide {
};

template<>
struct BookSide<true> {
    using MapType = std::map<int32_t, Limit *, std::greater<>>;
    //static constexpr bool is_bid = true;

};

template<>
struct BookSide<false> {
    using MapType = std::map<int32_t, Limit *, std::less<>>;
    //static constexpr bool is_bid = false;
};




class Orderbook {
private:


    DatabaseManager &db_manager_;
    OrderPool order_pool_;
    std::unordered_map<std::pair<int32_t, bool>, Limit *, boost::hash<std::pair<int32_t, bool>>> limit_lookup_;
    uint64_t bid_count_;
    uint64_t ask_count_;

    template<bool Side>
    typename BookSide<Side>::MapType &get_book_side();

    template<bool Side>
    Limit *get_or_insert_limit(int32_t price);

    template<bool Side>
    void update_vol(int32_t price, int32_t size, bool is_add);

    template<bool Side>
    void update_modify_vol(int32_t og_price, int32_t new_prive, int32_t og_size, int32_t new_size);

    static constexpr size_t BUFFER_SIZE = 40000;
    size_t write_index_ = 0;
    size_t size_ = 0;


public:
    int64_t ct_ = 0;
    std::vector<int32_t> mid_prices_;
    std::vector<int32_t> mid_prices_curr_;

    bool update_possible = false;
    BookSide<true>::MapType bids_;
    BookSide<false>::MapType offers_;
    std::unordered_map<uint64_t, Order *> order_lookup_;
    std::chrono::system_clock::time_point current_message_time_;
    double vwap_, sum1_, sum2_;
    float skew_, bid_depth_, ask_depth_;
    int32_t bid_vol_, ask_vol_;
    std::string last_reset_time_;
    double imbalance_;

    explicit Orderbook(DatabaseManager &db_manager);

    ~Orderbook();

    void reset();

    void calculate_skew();

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


    void calculate_imbalance();

    int32_t bid_delta_;
    int32_t ask_delta_;
    int32_t prev_best_bid_;
    int32_t prev_best_ask_;
    int32_t prev_best_bid_volume_;
    int32_t prev_best_ask_volume_;
    int32_t voi_;

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

    inline int32_t get_best_bid_volume() {
        return bids_.begin()->second->volume_;
    }

    inline int32_t get_best_ask_volume() {
        return offers_.begin()->second->volume_;
    }

    uint64_t get_bid_depth() const;

    uint64_t get_ask_depth() const;

    int32_t get_best_bid_price() const;

    int32_t get_best_ask_price() const;

    uint64_t get_count() const;

    std::string get_formatted_time_fast() const;

    template<bool Side>
    void add_limit_order(uint64_t id, int32_t price, uint32_t size, uint64_t unix_time);

    template<bool Side>
    void modify_order(uint64_t id, int32_t new_price, uint32_t new_size, uint64_t unix_time);

    template<bool Side>
    void trade_order(uint64_t id, int32_t price, uint32_t size);

    template<bool Side>
    void remove_order(uint64_t id, int32_t price, uint32_t size);

    template<bool Side>
    int32_t &get_volume();

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

    inline int32_t get_mid_price() {
        return (get_best_bid_price() + get_best_ask_price()) / 2;
    }


    inline void calculate_vols() {
        // vector register with 4 32 bit lanes
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
            // performs 4 load/add operations in 1 clock cycle
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

    std::vector<int32_t> voi_history_;
    std::vector<int32_t> voi_history_curr_;
};


#endif
