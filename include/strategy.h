#pragma once
#include <iostream>
#include <memory>
#include "orderbook.h"
#include "async_logger.h"


class Strategy {
protected:
    static bool req_fitting;
    int position_ = 0;
    int buy_qty_;
    int sell_qty_;
    int32_t real_total_buy_px_;
    int32_t real_total_sell_px_;
    int32_t theo_total_buy_px_;
    int32_t theo_total_sell_px_;
    float fees_;
    int32_t pnl_;
    static constexpr int max_pos_ = 1;
    static constexpr int32_t POINT_VALUE_ = 5;
    static constexpr int32_t FEES_PER_SIDE_= 1;
    int32_t prev_pnl_;
    DatabaseManager& db_manager_;
    std::unique_ptr<AsyncLogger> logger_;
    Orderbook* book_;

    //virtual void update_imbalance_stats(double imbalance) = 0;
    //virtual int calculate_trade_size(double imbalance) = 0;
    //virtual void execute_trade(bool is_buy, int32_t price, int size) = 0;
    virtual void update_theo_values() = 0;
    virtual void calculate_pnl() = 0;
    virtual void log_stats(const Orderbook& book) = 0;
    virtual void fit_model() = 0;


public:
    Strategy(DatabaseManager& db_manager, const std::string& log_file_name, Orderbook* book)
            : position_(0), buy_qty_(0), sell_qty_(0),
              real_total_buy_px_(0), real_total_sell_px_(0),
              theo_total_buy_px_(0), theo_total_sell_px_(0),
              fees_(0), pnl_(0), prev_pnl_(0), db_manager_(db_manager), book_(book) {
        logger_ = std::make_unique<AsyncLogger>(log_file_name, db_manager);
    }

    std::queue<std::tuple<bool, int32_t>> trade_queue_;

    virtual void reset() {
        position_ = 0;
        buy_qty_ = 0;
        sell_qty_ = 0;
        real_total_sell_px_ = 0;
        real_total_buy_px_ = 0;
        theo_total_buy_px_ = 0;
        theo_total_sell_px_ = 0;
        fees_ = 0;
        pnl_ = 0;
        prev_pnl_ = 0;
    }

    bool req_fitting_ = false;



    virtual ~Strategy() = default;

    virtual void on_book_update() = 0;
    virtual void execute_trade(bool side, int32_t price, int size) = 0;

    int32_t get_pnl() const { return pnl_; }
    int get_position() const { return position_; }
};