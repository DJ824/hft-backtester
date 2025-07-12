#pragma once
#include "../../include/message.h"
#include "../../include/slab_map.h"
#include "limit.h"
#include "limit_pool.h"
#include "order.h"
#include "order_pool.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>
#include <sys/stat.h>

class Orderbook {
private:
  const int32_t MIN_, MAX_;
  const size_t RANGE_;
  std::vector<Limit *> bids_;
  std::vector<Limit *> asks_;
  PageMap order_lookup_{1024};
  OrderPool order_pool_;
  LimitPool limit_pool_;
  int32_t best_bid_idx_, best_ask_idx_;
  int32_t bid_vol_;
  int32_t ask_vol_;
  int64_t sum1_;
  int64_t sum2_;
  double vwap_;
  double imbalance_;
  std::chrono::system_clock::time_point current_message_time_;
  int32_t bid_delta_ = 0;
  int32_t ask_delta_ = 0;
  int32_t prev_best_bid_ = 0;
  int32_t prev_best_ask_ = 0;
  int32_t prev_best_bid_volume_ = 0;
  int32_t prev_best_ask_volume_ = 0;
  int32_t voi_ = 0;

  inline size_t get_bid_idx(int32_t px) const { return MAX_ - px; }
  inline size_t get_ask_idx(int32_t px) const { return px - MIN_; }

  template <bool Side>
  inline Limit *&slot(int32_t px) {
    return Side ? bids_[get_bid_idx(px)] : asks_[get_ask_idx(px)];
  }

  template <bool Side>
  inline void adjust_bbo();

public:
  std::vector<int32_t> mid_prices_;
  std::vector<int32_t> mid_prices_curr_;
  std::vector<int32_t> voi_history_;
  std::vector<int32_t> voi_history_curr_;
  inline Orderbook(int32_t min_px, int32_t max_px);
  template <bool Side>
  inline Limit *get_or_insert_limit(int32_t price);
  inline void process_msg(const book_message &m);
  inline void print_top_levels(std::size_t depth = 10) const;
  inline void calculate_vols(size_t ct = 5);
  inline void calculate_imbalance();
  inline void calculate_vwap(int32_t price, int32_t size);
  inline void calculate_voi();
  inline void calculate_voi_curr();
  inline void add_mid_price();
  inline void add_mid_price_curr();
  inline int32_t best_bid() const;
  inline int32_t best_ask() const;
  inline int32_t get_best_bid_price() const;
  inline int32_t get_best_ask_price() const;
  inline int32_t get_mid_price() const;
  inline size_t get_best_ask_index() const;
  inline size_t get_best_bid_index() const;
  inline double get_imbalance() const;
  inline double get_vwap() const;
  inline std::string get_formatted_time_fast() const;
  template <bool Side>
  inline void add_order(uint64_t id, int32_t price,
                        uint32_t sz, uint64_t ts);

  template <bool Side>
  inline void modify_order(uint64_t id, int32_t price,
                           uint32_t sz, uint64_t ts);
  template <bool Side>
  inline void cancel_order(uint64_t id, int32_t /*px*/,
                           uint32_t /*sz*/);
};


inline Orderbook::Orderbook(int32_t min_px, int32_t max_px)
  : MIN_(min_px), MAX_(max_px), RANGE_(static_cast<size_t>(MAX_ - MIN_ + 1)),
    bids_(RANGE_, nullptr), asks_(RANGE_, nullptr),
    best_bid_idx_(static_cast<int32_t>(RANGE_)),
    best_ask_idx_(static_cast<int32_t>(RANGE_)), bid_vol_(0), ask_vol_(0),
    sum1_(0), sum2_(0), vwap_(0), imbalance_(0) {
}

template <bool Side>
inline Limit *Orderbook::get_or_insert_limit(int32_t price) {
  Limit *&limit = slot<Side>(price);
  if (limit) {
    return limit;
  }
  limit = limit_pool_.acquire(price, Side);
  return limit;
}

inline void Orderbook::process_msg(const book_message &m) {
  if (m.price_ < 1'000'00 || m.price_ > 8'000'00)
    return;

  // current_message_time_ =
  //     std::chrono::system_clock::time_point(
  //         std::chrono::duration_cast<std::chrono::microseconds>(
  //             std::chrono::nanoseconds(m.time_)));

  const bool is_bid = m.side_ == 1;

  switch (m.action_) {
  case 'A':
    is_bid
      ? add_order<true>(m.id_, m.price_, m.size_, m.time_)
      : add_order<false>(m.id_, m.price_, m.size_, m.time_);
    break;

  case 'M':
    is_bid
      ? modify_order<true>(m.id_, m.price_, m.size_, m.time_)
      : modify_order<false>(m.id_, m.price_, m.size_, m.time_);
    break;

  case 'C':
    is_bid
      ? cancel_order<true>(m.id_, m.price_, m.size_)
      : cancel_order<false>(m.id_, m.price_, m.size_);
    break;

    // case 'T':
    //   calculate_vwap(m.price_, m.size_);
    //   break;
  }
}


template <bool Side>
inline void Orderbook::add_order(uint64_t id, int32_t price,
                                 uint32_t sz, uint64_t ts) {
  Limit *limit = get_or_insert_limit<Side>(price);

  Order *new_order = order_pool_.get_order();
  new_order->id_ = id;
  new_order->price_ = price;
  new_order->size = sz;
  new_order->side_ = Side;
  new_order->unix_time_ = ts;
  new_order->parent_ = limit;

  order_lookup_.insert(id, new_order);
  limit->add_order(new_order);

  if constexpr (Side) {
    best_bid_idx_ = std::min(best_bid_idx_,
                             static_cast<int32_t>(get_bid_idx(price)));
  } else {
    best_ask_idx_ = std::min(best_ask_idx_,
                             static_cast<int32_t>(get_ask_idx(price)));
  }
}


template <bool Side>
inline void Orderbook::modify_order(uint64_t id,
                                    int32_t price,
                                    uint32_t sz,
                                    uint64_t ts) {
  Order *target = order_lookup_.find(id);
  if (!target) {
    add_order<Side>(id, price, sz, ts);
    return;
  }

  const int32_t old_price = target->price_;
  const int32_t old_size = target->size;

  if (old_price != price || sz > old_size) {
    cancel_order<Side>(id, old_price, old_size);
    add_order<Side>(id, price, sz, ts);
    return;
  }

  if (sz < old_size) {
    int32_t diff = static_cast<int32_t>(sz) - old_size;
    target->parent_->volume_ += diff;
    target->size = sz;
  }
  target->unix_time_ = ts;
}


template <bool Side>
inline void Orderbook::cancel_order(uint64_t id, int32_t price,
                                    uint32_t size) {
  Order *target = order_lookup_.find(id);
  if (!target) {
    return;
  }
  Limit *limit = target->parent_;
  limit->remove_order(target);
  order_lookup_.erase(id);
  order_pool_.return_order(target);

  if (!limit->is_empty()) {
    return;
  }

  if constexpr (Side) {
    bids_[get_bid_idx(limit->price_)] = nullptr;
  } else {
    asks_[get_ask_idx(limit->price_)] = nullptr;
  }

  limit_pool_.release(limit);
  adjust_bbo<Side>();
}

template <bool Side>
inline void Orderbook::adjust_bbo() {
  if constexpr (Side) {
    while (best_bid_idx_ < static_cast<int32_t>(RANGE_) &&
           bids_[best_bid_idx_] == nullptr)
      ++best_bid_idx_;
  } else {
    while (best_ask_idx_ < static_cast<int32_t>(RANGE_) &&
           asks_[best_ask_idx_] == nullptr)
      ++best_ask_idx_;
  }
}

inline void Orderbook::calculate_vols(size_t ct) {
  bid_vol_ = 0;
  ask_vol_ = 0;
  size_t bid_idx = best_bid_idx_;
  size_t ask_idx = best_ask_idx_;

  for (size_t i = 0; i < ct && bid_idx < RANGE_ && ask_idx < RANGE_; i++) {
    if (bids_[bid_idx])
      bid_vol_ += bids_[bid_idx]->volume_;
    if (asks_[ask_idx])
      ask_vol_ += asks_[ask_idx]->volume_;
    ++bid_idx;
    ++ask_idx;
  }
}

inline void Orderbook::calculate_imbalance() {
  uint64_t total_vol = bid_vol_ + ask_vol_;
  if (total_vol > 0) {
    imbalance_ = (static_cast<double>(bid_vol_) - static_cast<double>(ask_vol_))
                 /
                 static_cast<double>(total_vol);
  } else {
    imbalance_ = 0.0;
  }
}

inline void Orderbook::calculate_vwap(int32_t price, int32_t size) {
  int32_t og_price = price /= 100.0;
  sum1_ += (og_price * size);
  sum2_ += size;
  vwap_ = static_cast<double>(sum1_) / static_cast<double>(sum2_);
}

inline void Orderbook::calculate_voi() {
  int32_t bid_voi = 0;
  int32_t ask_voi = 0;
  int32_t bid_price = get_best_bid_price();
  int32_t ask_price = get_best_ask_price();
  int32_t bid_vol = bids_[best_bid_idx_] ? bids_[best_bid_idx_]->volume_ : 0;
  int32_t ask_vol = asks_[best_ask_idx_] ? asks_[best_ask_idx_]->volume_ : 0;

  if (bid_price == prev_best_bid_) {
    bid_voi = bid_vol - prev_best_bid_volume_;
  } else if (bid_price > prev_best_bid_) {
    bid_voi = bid_vol;
  }

  if (ask_price == prev_best_ask_) {
    ask_voi = ask_vol - prev_best_ask_volume_;
  } else if (ask_price < prev_best_ask_) {
    ask_voi = ask_vol;
  }

  voi_ = bid_voi - ask_voi;
  voi_history_.push_back(voi_);
  prev_best_bid_ = bid_price;
  prev_best_ask_ = ask_price;
  prev_best_bid_volume_ = bid_vol;
  prev_best_ask_volume_ = ask_vol;
  std::cout << voi_ << std::endl;
  std::cout << prev_best_bid_ << std::endl;
  std::cout << prev_best_ask_ << std::endl;
  std::cout << prev_best_ask_volume_ << std::endl;
  std::cout << prev_best_bid_volume_ << std::endl;
}

inline void Orderbook::calculate_voi_curr() {
  int32_t bid_voi = 0;
  int32_t ask_voi = 0;
  int32_t bid_price = get_best_bid_price();
  int32_t ask_price = get_best_ask_price();
  int32_t bid_vol = bids_[best_bid_idx_] ? bids_[best_bid_idx_]->volume_ : 0;
  int32_t ask_vol = asks_[best_ask_idx_] ? asks_[best_ask_idx_]->volume_ : 0;

  if (bid_price == prev_best_bid_) {
    bid_voi = bid_vol - prev_best_bid_volume_;
  } else if (bid_price > prev_best_bid_) {
    bid_voi = bid_vol;
  }

  if (ask_price == prev_best_ask_) {
    ask_voi = ask_vol - prev_best_ask_volume_;
  } else if (ask_price < prev_best_ask_) {
    ask_voi = ask_vol;
  }

  voi_ = bid_voi - ask_voi;
  voi_history_curr_.push_back(voi_);
  prev_best_bid_ = bid_price;
  prev_best_ask_ = ask_price;
  prev_best_bid_volume_ = bid_vol;
  prev_best_ask_volume_ = ask_vol;
}

inline void Orderbook::add_mid_price() {
  mid_prices_.push_back(get_mid_price());
}

inline void Orderbook::add_mid_price_curr() {
  mid_prices_curr_.push_back(get_mid_price());
}

inline void Orderbook::print_top_levels(std::size_t depth) const {
  std::cout << "\nprice\tbid_vol\t|\task_vol\tprice\n"
      << "-----------------------------------------------\n";

  std::size_t bid_idx = best_bid_idx_;
  std::size_t ask_idx = best_ask_idx_;

  for (std::size_t printed = 0; printed < depth; ++printed) {
    const Limit *bid = nullptr;
    while (bid_idx < RANGE_ && !((bid = bids_[bid_idx]))) {
      ++bid_idx;
    }

    if (bid) {
      std::cout << std::setw(9) << bid->price_ << '\t' << std::setw(6) << bid->
          volume_;
    } else {
      std::cout << std::setw(9) << '-' << '\t' << std::setw(6) << '-';
    }

    std::cout << "\t|\t";

    const Limit *ask = nullptr;
    while (ask_idx < RANGE_ && !(ask = asks_[ask_idx])) {
      ++ask_idx;
    }

    if (ask) {
      std::cout << std::setw(6) << ask->volume_ << '\t' << std::setw(9) << ask->
          price_;
    } else {
      std::cout << std::setw(6) << '-' << '\t' << std::setw(9) << '-';

    }
    std::cout << '\n';
    ++bid_idx;
    ++ask_idx;
  }
}

inline int32_t Orderbook::best_bid() const {
  return (best_bid_idx_ >= static_cast<int32_t>(RANGE_))
           ? 0
           : MAX_ - best_bid_idx_;
}

inline int32_t Orderbook::best_ask() const {
  return (best_ask_idx_ >= static_cast<int32_t>(RANGE_))
           ? 0
           : MIN_ + best_ask_idx_;
}

inline int32_t Orderbook::get_best_bid_price() const {
  return (best_bid_idx_ < static_cast<int32_t>(RANGE_) && bids_[best_bid_idx_])
           ? bids_[best_bid_idx_]->price_
           : 0;
}

inline int32_t Orderbook::get_best_ask_price() const {
  return (best_ask_idx_ < static_cast<int32_t>(RANGE_) && asks_[best_ask_idx_])
           ? asks_[best_ask_idx_]->price_
           : 0;
}

inline int32_t Orderbook::get_mid_price() const {
  int32_t bid = get_best_bid_price();
  int32_t ask = get_best_ask_price();
  return ((bid + ask) / 2) / 100;
}

inline size_t Orderbook::get_best_ask_index() const {
  return best_ask_idx_;
}

inline size_t Orderbook::get_best_bid_index() const {
  return best_bid_idx_;
}

inline double Orderbook::get_imbalance() const {
  return imbalance_;
}

inline double Orderbook::get_vwap() const {
  return vwap_;
}

inline std::string Orderbook::get_formatted_time_fast() const {
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
    strftime(last_second_str, sizeof(last_second_str), "%Y-%m-%d %H:%M:%S",
             &tm_buf);
  }

  snprintf(buffer, sizeof(buffer), "%s.%03d", last_second_str,
           static_cast<int>(ms.count()));
  return std::string(buffer);
}