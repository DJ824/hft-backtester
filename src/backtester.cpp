#include "backtester.h"
#include "strategies/imbalance_strat.cpp"
#include "strategies/linear_model_strat.cpp"

Backtester::Backtester(std::shared_ptr<ConnectionPool> pool,
                       const std::string& instrument_id,
                        std::vector<book_message>&& messages,
                        std::vector<book_message>&& train_messages)
        : connection_pool_(pool)
        , instrument_id_(instrument_id)
        , messages_(std::move(messages))
        , train_messages_(std::move(train_messages))
        , first_update_(false)
        , current_message_index_(0)
        , train_message_index_(0)
        , running_(false) {
    book_ = std::make_unique<Orderbook>();
    train_book_ = std::make_unique<Orderbook>();
}

Backtester::~Backtester() {
    stop_backtest();
}

void Backtester::create_strategy(size_t strategy_index) {
    strategy_ = nullptr;
    switch (strategy_index) {
        case 0:
            strategy_ = std::make_unique<ImbalanceStrat>(
                    connection_pool_, instrument_id_, book_.get());
            break;
        case 1:
            strategy_ = std::make_unique<LinearModelStrategy>(
                    connection_pool_, instrument_id_, book_.get());
            break;
        default:
            throw std::runtime_error("unknown strategy index: " +
                                     std::to_string(strategy_index));
    }
}

void Backtester::set_trading_times(const std::string& backtest_file, const std::string& train_file) {
    auto extract_date = [](const std::string& filename) -> std::string {
        if (filename.length() < 8) return "";
        std::string month = filename.substr(2, 2);
        std::string day = filename.substr(4, 2);
        return "2024-" + month + "-" + day;
    };

    std::string backtest_date = extract_date(backtest_file);
    if (!backtest_date.empty()) {
        start_time_ = backtest_date + " 09:30:00.000";
        end_time_ = backtest_date + " 16:00:00.000";
    }

    if (!train_file.empty()) {
        std::string train_date = extract_date(train_file);
        if (!train_date.empty()) {
            train_start_time_ = train_date + " 09:30:00.000";
            train_end_time_ = train_date + " 16:00:00.000";
        }
    }
}

void Backtester::train_model() {
    train_message_index_ = 0;
    int64_t prev_seconds = 0;

    auto parse_time = [](const std::string& time_str) {
        int hour = (time_str[11] - '0') * 10 + (time_str[12] - '0');
        int minute = (time_str[14] - '0') * 10 + (time_str[15] - '0');
        int second = (time_str[17] - '0') * 10 + (time_str[18] - '0');
        return hour * 3600 + minute * 60 + second;
    };

    while (train_message_index_ < train_messages_.size()) {
        const auto& msg = train_messages_[train_message_index_];
        train_book_->process_msg(msg);

        std::string curr_time = train_book_->get_formatted_time_fast();
        int64_t curr_seconds = parse_time(curr_time);

        if (curr_time >= train_start_time_) {
            if (prev_seconds == 0) {
                prev_seconds = curr_seconds;
            }

            if (curr_seconds - prev_seconds >= 1) {
                train_book_->calculate_voi();
                train_book_->add_mid_price();
                prev_seconds = curr_seconds;
            }
        }

        ++train_message_index_;

        if (curr_time >= train_end_time_) {
            break;
        }
    }

    book_->voi_history_ = std::move(train_book_->voi_history_);
    book_->mid_prices_ = std::move(train_book_->mid_prices_);

    if (strategy_->requires_fitting()) {
        strategy_->fit_model();
    }
}

void Backtester::start_backtest() {
    if (!running_) {
        running_ = true;
        run_backtest();
    }
}

void Backtester::stop_backtest() { running_ = false; }


void Backtester::run_backtest() {
    auto parse_time = [](const std::string& time_str) {
        int hour = (time_str[11] - '0') * 10 + (time_str[12] - '0');
        int minute = (time_str[14] - '0') * 10 + (time_str[15] - '0');
        int second = (time_str[17] - '0') * 10 + (time_str[18] - '0');
        return hour * 3600 + minute * 60 + second;
    };

    int64_t prev_seconds = 0;

    while (running_ && current_message_index_ < messages_.size()) {
        const auto& msg = messages_[current_message_index_];
        book_->process_msg(msg);

        std::string curr_time = book_->get_formatted_time_fast();
        int64_t curr_seconds = parse_time(curr_time);

        if (curr_time >= start_time_) {
            if (prev_seconds == 0) {
                prev_seconds = curr_seconds;
            }

            if (curr_seconds - prev_seconds >= 1) {
                strategy_->on_book_update();
                prev_seconds = curr_seconds;
            }
        }

        if (curr_time >= end_time_) {
            strategy_->close_positions();
            break;
        }

        ++current_message_index_;

    }

    running_ = false;
}

void Backtester::reset_state() {
    running_ = false;
    current_message_index_ = 0;
    train_message_index_ = 0;
    first_update_ = false;
    book_.reset();

    train_book_.reset();

    if (strategy_) {
        strategy_->reset();
    }
}

void Backtester::run_multiday_backtest() {
    while (!trading_days_.empty()) {
        current_day_ = std::move(trading_days_.front());
        trading_days_.pop();

        start_time_ = current_day_.start_time_;
        end_time_ = current_day_.end_time_;
    }
}
