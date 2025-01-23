#pragma once
#include <memory>
#include <map>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "backtester.h"
#include "connection_pool.h"

class ConcurrentBacktester {
private:
    struct InstrumentConfig {
        std::string instrument_id;
        std::unique_ptr<Backtester> backtester;
        std::vector<message> messages;
        std::vector<message> train_messages;
        int32_t pnl{0};
        std::string backtest_file;
        std::string train_file;
        std::thread thread;
    };

    static std::shared_ptr<ConnectionPool> connection_pool_;
    static std::once_flag pool_init_flag_;
    std::map<std::string, InstrumentConfig> instruments_;
    std::atomic<bool> running_{false};
    std::atomic<int> completed_count_{0};
    std::mutex completion_mutex_;
    std::mutex cout_mutex_;

public:
    explicit ConcurrentBacktester();
    ~ConcurrentBacktester();

    ConcurrentBacktester(const ConcurrentBacktester&) = delete;
    ConcurrentBacktester& operator=(const ConcurrentBacktester&) = delete;

    void add_instrument(const std::string& instrument_id,
                        const std::vector<message>& messages,
                        const std::vector<message>& train_messages = {},
                        const std::string& backtest_file = "",
                        const std::string& train_file = "");
    void start_backtest(size_t strategy_index);
    void stop_backtest();
};
