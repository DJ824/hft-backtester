#pragma once
#include <memory>
#include <vector>
#include "strategy.h"
#include "../src/book/orderbook.cpp"
#include "message.h"

struct TradingDay {
    std::vector<book_message> messages_;
    std::string date_;
    std::string start_time_;
    std::string end_time_;
    std::string file_;
};

class Backtester {
public:
    Backtester(std::shared_ptr<ConnectionPool> pool,
               const std::string& instrument_id,
                std::vector<book_message>&& messages,
                std::vector<book_message>&& train_messages = {});
    ~Backtester();

    void create_strategy(size_t strategy_index);
    void set_trading_times(const std::string& backtest_file, const std::string& train_file = "");
    void train_model();
    void start_backtest();
    void stop_backtest();
    void reset_state();

private:
    void run_backtest();
    void run_multiday_backtest();

    std::queue<TradingDay> trading_days_;
    TradingDay current_day_;
    std::shared_ptr<Orderbook> book_;
    std::unique_ptr<Orderbook> train_book_;
    size_t train_message_index_;
    std::unique_ptr<Strategy> strategy_;
    std::shared_ptr<ConnectionPool> connection_pool_;
    std::string instrument_id_;
    Connection* db_connection_;
    bool first_update_;
    size_t current_message_index_;
    std::atomic<bool> running_;
    std::vector<book_message> messages_;
    std::vector<book_message> train_messages_;
    std::string start_time_;
    std::string end_time_;
    std::string train_start_time_;
    std::string train_end_time_;

    static constexpr int UPDATE_INTERVAL = 1000;
};