#pragma once

#include <iostream>
#include <memory>
#include <queue>
#include <vector>
#include <array>
#include <chrono>
#include <cmath>
#include <numeric>
#include <Eigen/Dense>
#include "strategy.h"
#include "orderbook.h"
#include "async_logger.h"

class LinearModelStrategy : public Strategy {
protected:
    static constexpr int MAX_LAG_ = 5;
    static constexpr int FORECAST_WINDOW_ = 120;
    static constexpr double THRESHOLD_ = 10;
    static constexpr int TRADE_SIZE_ = 1;

    std::vector<double> model_coefficients_;
    int forecast_window_;
    double fees_ = 0.0;


    [[nodiscard]] double predict_price_change() const {

        size_t data_size = static_cast<int>(book_->voi_history_curr_.size());

        if (data_size < MAX_LAG_ + 1) {
            return 0.0;
        }

        double prediction = model_coefficients_[0];

        for (int i = 0; i <= MAX_LAG_; ++i) {
            size_t index = data_size - 1 - i;
            auto voi = static_cast<double>(book_->voi_history_curr_[index]);
            prediction += model_coefficients_[i + 1] * voi;
        }

       // std::cout << prediction << std::endl;
       // std::cout << book_->get_formatted_time_fast() << std::endl;

        return prediction;
    }


    void update_theo_values() override {
        int32_t bid_price = book_->get_best_bid_price();
        int32_t ask_price = book_->get_best_ask_price();

        if (position_ == 0) {
            theo_total_buy_px_ = 0;
            theo_total_sell_px_ = 0;
        } else if (position_ > 0) {
            theo_total_sell_px_ = bid_price * std::abs(position_);
            theo_total_buy_px_ = 0;
        } else if (position_ < 0) {
            theo_total_buy_px_ = ask_price * std::abs(position_);
            theo_total_sell_px_ = 0;
        }
    }

    void calculate_pnl() override {
        pnl_ = POINT_VALUE_ * (real_total_sell_px_ + theo_total_sell_px_ -
                               real_total_buy_px_ - theo_total_buy_px_) - fees_;
    }

    void log_stats(const Orderbook& book) override {
        std::string timestamp = book.get_formatted_time_fast();
        int32_t bid = book.get_best_bid_price();
        int32_t ask = book.get_best_ask_price();
        int trade_count = buy_qty_ + sell_qty_;

        logger_->log(timestamp, bid, ask, position_, trade_count, pnl_);
    }

public:

    bool req_fitting = true;

    explicit LinearModelStrategy(DatabaseManager& db_manager, Orderbook* book)
            : Strategy(db_manager, "linear_model_strategy_log.csv", book),
              forecast_window_(FORECAST_WINDOW_), fees_(0.0) {

        model_coefficients_.resize(MAX_LAG_ + 2, 0.0);
    }

    void execute_trade(bool is_buy, int32_t price, int32_t trade_size) override {
        if (is_buy) {
            trade_queue_.emplace(true, price);
            position_ += TRADE_SIZE_;
            buy_qty_ += TRADE_SIZE_;
            real_total_buy_px_ += price * TRADE_SIZE_;
        } else {
            trade_queue_.emplace(false, price);
            position_ -= TRADE_SIZE_;
            sell_qty_ += TRADE_SIZE_;
            real_total_sell_px_ += price * TRADE_SIZE_;
        }
        fees_ += FEES_PER_SIDE_;
    }


    void on_book_update() override {

        book_->calculate_voi_curr();
        book_->add_mid_price_curr();

        double predicted_change = predict_price_change();

        int32_t bid_price = book_->get_best_bid_price();
        int32_t ask_price = book_->get_best_ask_price();

        if (predicted_change >= THRESHOLD_ && position_ < max_pos_) {
            std::cout << predicted_change << std::endl;
            std::cout << book_->get_formatted_time_fast() << std::endl;
            execute_trade(true, ask_price, 1);
        } else if (predicted_change <= -THRESHOLD_ && position_ > -max_pos_) {
            std::cout << predicted_change << std::endl;
            std::cout << book_->get_formatted_time_fast() << std::endl;
            execute_trade(false, bid_price, 1);
        }

        update_theo_values();
        calculate_pnl();

    }

    void reset() override {
        Strategy::reset();
        model_coefficients_.clear();
        model_coefficients_.resize(MAX_LAG_ + 2, 0.0);
        position_ = 0;
        pnl_ = 0.0;
        fees_ = 0.0;
        prev_pnl_ = 0.0;
    }

    void fit_model() override {
        std::cout << book_->voi_history_.size() << std::endl;
        int n = static_cast<int>(book_->voi_history_.size()) - forecast_window_ - MAX_LAG_;

        Eigen::MatrixXd X(n, MAX_LAG_ + 2);
        Eigen::VectorXd y(n);

        for (int i = 0; i < n; ++i) {
            X(i, 0) = 1.0;

            for (int j = 0; j <= MAX_LAG_; ++j) {
                double voi = static_cast<double>(book_->voi_history_[i + j]);
                X(i, j + 1) = voi;
            }

            double avg_mid_change = 0.0;
            for (int k = 1; k <= forecast_window_; ++k) {
                avg_mid_change += static_cast<double>(book_->mid_prices_[i + MAX_LAG_ + k] - book_->mid_prices_[i + MAX_LAG_]);
            }
            avg_mid_change /= forecast_window_;
            y(i) = avg_mid_change;
        }

        Eigen::VectorXd coeffs = X.colPivHouseholderQr().solve(y);

        model_coefficients_ = std::vector<double>(coeffs.data(), coeffs.data() + coeffs.size());

        std::cout << "model coefficients:" << std::endl;
        for (size_t i = 0; i < model_coefficients_.size(); ++i) {
            std::cout << "coeff[" << i << "]: " << model_coefficients_[i] << std::endl;
        }
    }

};
