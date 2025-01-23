
#include "strategy.h"

class ImbalanceStrat : public Strategy {
private:
    double imbalance_mean_ = 0.0;
    double imbalance_variance_ = 0.0;
    int update_count_ = 0;
    const int WARMUP_PERIOD = 1000;

protected:

    void fit_model() override {

    }




    void update_theo_values() override {
        if (position_ == 0) {
            theo_total_buy_px_ = theo_total_sell_px_ = 0;
        } else if (position_ > 0) {
            theo_total_sell_px_ = book_->get_best_bid_price() * std::abs(position_);
            theo_total_buy_px_ = 0;
        } else {
            theo_total_buy_px_ = book_->get_best_ask_price() * std::abs(position_);
            theo_total_sell_px_ = 0;
        }
    }

    void calculate_pnl() override {
        pnl_ = POINT_VALUE_ * (real_total_sell_px_ + theo_total_sell_px_ -
                               real_total_buy_px_ - theo_total_buy_px_) - fees_;
    }



public:
    explicit ImbalanceStrat(std::shared_ptr<ConnectionPool> pool,
                            const std::string& instrument_id,
                            Orderbook* book)
            : Strategy(pool, "imbalance_strat_log.csv", instrument_id, book) {
        name_ = "imbalance_strat";
        req_fitting_ = false;
    }

    void on_book_update() override {

        auto imbalance = book_->imbalance_;

        auto mid_price = book_->get_mid_price();

        if (imbalance > 0 && book_->get_mid_price() < book_->vwap_ && position_ + 1 <= max_pos_) {
            execute_trade(true, book_->get_best_ask_price(), 1);
            trade_queue_.emplace(true, book_->get_best_ask_price());

        } else if (imbalance < 0 && book_->get_mid_price() > book_->vwap_&& position_ - 1 >= -max_pos_) {
            execute_trade(false, book_->get_best_bid_price(), 1);
            trade_queue_.emplace(false, book_->get_best_bid_price());
        }

        update_theo_values();
        calculate_pnl();
    }

    void log_stats(const Orderbook &book) override {
        std::string timestamp = book.get_formatted_time_fast();
        auto bid = book.get_best_bid_price();
        auto ask = book.get_best_ask_price();
        int trade_count = buy_qty_ + sell_qty_;

        logger_->log(timestamp, bid, ask, position_, trade_count, pnl_);
    }


    void execute_trade(bool is_buy, int32_t price, int size) override {
        if (is_buy) {
            position_ += size;
            buy_qty_ += size;
            real_total_buy_px_ += price * size;
        } else {
            position_ -= size;
            sell_qty_ += size;
            real_total_sell_px_ += price * size;
        }
        fees_ += FEES_PER_SIDE_;
    }



    void reset() override {
        Strategy::reset();
        imbalance_mean_ = 0.0;
        imbalance_variance_ = 0.0;
        update_count_ = 0;
    }
};