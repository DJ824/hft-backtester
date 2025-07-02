#include "../../include/async_logger.h"
#include "../../include/strategy.h"
#include "../../include/book/orderbook.h"
#include <Eigen/Dense>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <numeric>
#include <queue>
#include <vector>

class LinearModelStrategy : public Strategy {
protected:
  static constexpr int MAX_LAG_ = 5;
  static constexpr int FORECAST_WINDOW_ = 60;
  int THRESHOLD_;
  static constexpr int TRADE_SIZE_ = 1;
  std::mutex fit_mutex_;
  std::mutex log_mutex_;

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
                           real_total_buy_px_ - theo_total_buy_px_) -
           fees_;
  }

public:
  explicit LinearModelStrategy(std::shared_ptr<ConnectionPool> pool,
                               const std::string &instrument_id,
                               Orderbook *book)
      : Strategy(pool, "linear_model_strategy_log.csv", instrument_id, book),
        forecast_window_(FORECAST_WINDOW_), fees_(0.0) {
    model_coefficients_.resize(MAX_LAG_ + 2, 0.0);
    name_ = "linear_model_strat";
    req_fitting_ = true;
    if (instrument_id == "es") {
      POINT_VALUE_ = 5;
      THRESHOLD_ = 2;
    } else {
      POINT_VALUE_ = 2;
      THRESHOLD_ = 20;
    }
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

  void log_stats(const Orderbook &book) override {
    std::string timestamp = book.get_formatted_time_fast();
    const int32_t bid = book_->get_best_bid_price();
    const int32_t ask = book_->get_best_ask_price();
    int trade_count = buy_qty_ + sell_qty_;

    logger_->log(timestamp, bid, ask, position_, trade_count, pnl_);
  }

  void on_book_update() override {

    book_->calculate_voi_curr();
    book_->add_mid_price_curr();

    double predicted_change = predict_price_change();

    int32_t bid_price = book_->get_best_bid_price();
    int32_t ask_price = book_->get_best_ask_price();

    if (predicted_change >= THRESHOLD_ && position_ < max_pos_) {
      // std::cout << predicted_change << std::endl;
      // std::cout << book_->get_formatted_time_fast() << std::endl;
      execute_trade(true, ask_price, 1);
      log_stats(*book_);
    } else if (predicted_change <= -THRESHOLD_ && position_ > -max_pos_) {
      // std::cout << predicted_change << std::endl;
      // std::cout << book_->get_formatted_time_fast() << std::endl;
      execute_trade(false, bid_price, 1);
      log_stats(*book_);
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
    std::lock_guard<std::mutex> lock(fit_mutex_);
    int n = static_cast<int>(book_->voi_history_.size()) - forecast_window_ -
            MAX_LAG_;

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
        avg_mid_change +=
            static_cast<double>(book_->mid_prices_[i + MAX_LAG_ + k] -
                                book_->mid_prices_[i + MAX_LAG_]);
      }
      avg_mid_change /= forecast_window_;
      y(i) = avg_mid_change;
    }

    Eigen::VectorXd coeffs = X.colPivHouseholderQr().solve(y);

    model_coefficients_ =
        std::vector<double>(coeffs.data(), coeffs.data() + coeffs.size());

    {
      std::lock_guard<std::mutex> log_lock(log_mutex_);

      for (size_t i = 0; i < model_coefficients_.size(); ++i) {
        std::cout << "coeff[" << i << "]: " << model_coefficients_[i]
                  << std::endl;
      }
    }
  }

  void close_positions() override {
    int initial_position = position_;

    if (position_ != 0) {
      while (position_ > 0) {
        int32_t close_price = book_->get_best_bid_price();

        execute_trade(false, close_price, 1);

        log_stats(*book_);

        update_theo_values();
        calculate_pnl();
      }

      while (position_ < 0) {
        int32_t close_price = book_->get_best_ask_price();

        execute_trade(true, close_price, 1);

        log_stats(*book_);

        update_theo_values();
        calculate_pnl();
      }

      if (initial_position != 0) {
        std::string timestamp = book_->get_formatted_time_fast();
        logger_->log(timestamp, book_->get_best_bid_price(),
                     book_->get_best_ask_price(), position_,
                     buy_qty_ + sell_qty_, pnl_);
      }

      assert(position_ == 0);
    }
  }
};

/*
*2024-08-15 11:43:06.268 | instrument: nq | position: 0 | bid/ask:
1977125/1977375 | pnl: -32613.00 2024-08-15 12:01:25.391 | instrument: nq |
position: -1 | bid/ask: 1975825/1976100 | pnl: -32914.00 2024-08-15 13:02:21.242
| instrument: nq | position: 0 | bid/ask: 1976575/1976850 | pnl: -34965.00
2024-08-15 13:13:19.100 | instrument: nq | position: 1 | bid/ask:
1976300/1976475 | pnl: -34966.00 2024-08-15 13:29:37.057 | instrument: nq |
position: 0 | bid/ask: 1977250/1977450 | pnl: -33417.00 2024-08-15 13:29:38.191
| instrument: nq | position: -1 | bid/ask: 1977200/1977925 | pnl: -33418.00
2024-08-15 15:16:03.051 | instrument: nq | position: 0 | bid/ask:
1981175/1981400 | pnl: -41769.00 2024-08-15 15:31:07.166 | instrument: nq |
position: -1 | bid/ask: 1981550/1981775 | pnl: -41820.00 [0x16f7cf000] nq
completed 2024-08-15 15:59:06.006 | instrument: nq | position: 0 | bid/ask:
1979850/1980975 | pnl: -40671.00 2024-08-15 15:59:56.090 | instrument: nq |
position: 1 | bid/ask: 1981575/1982800 | pnl: -40672.00 2024-08-15 16:00:00.015
| instrument: nq | position: 0 | bid/ask: 1981625/1982800 | pnl: -43023.00
2024-08-15 16:00:00.015 | instrument: nq | position: 0 | bid/ask:
1981625/1982800 | pnl: -43024.00 coeff[0]: -26.11 coeff[1]: 0.03 coeff[2]: 0.02
coeff[3]: 0.02
coeff[4]: 0.02
coeff[5]: 0.02
coeff[6]: 0.08
[0x16f513000] es: training complete
2024-08-02 09:30:06.002 | instrument: es | position: -1 | bid/ask: 540050/540075
| pnl: 0.00 [0x16f513000] es completed backtest complete 2024-08-02 16:00:00.000
| instrument: es | position: 0 | bid/ask: 537625/537650 | pnl: 11999.00
2024-08-02 16:00:00.000 | instrument: es | position: 0 | bid/ask: 537625/537650
| pnl: 11998.00
*/
