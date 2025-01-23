#include "concurrent_backtest.h"
#include <iostream>
#include <future>

std::shared_ptr<ConnectionPool> ConcurrentBacktester::connection_pool_ = nullptr;
std::once_flag ConcurrentBacktester::pool_init_flag_;

ConcurrentBacktester::ConcurrentBacktester() {
    std::call_once(pool_init_flag_, []() {
        connection_pool_ = std::make_shared<ConnectionPool>("127.0.0.1", 9009);
    });
}

ConcurrentBacktester::~ConcurrentBacktester() {
    stop_backtest();
}

void ConcurrentBacktester::add_instrument(const std::string& instrument_id,
                                         const std::vector<message>& messages,
                                         const std::vector<message>& train_messages,
                                         const std::string& backtest_file,
                                         const std::string& train_file) {
    auto& config = instruments_[instrument_id];
    config.instrument_id = instrument_id;
    config.messages = messages;
    config.train_messages = train_messages;
    config.backtest_file = backtest_file;
    config.train_file = train_file;

    config.backtester = std::make_unique<Backtester>(
            connection_pool_,
            instrument_id,
            config.messages,
            config.train_messages
    );
}


void ConcurrentBacktester::stop_backtest() {
    running_ = false;
    for(auto& [id, config] : instruments_) {
        config.backtester->stop_backtest();
        if(config.thread.joinable()) {
            config.thread.join();
        }
    }
}

void ConcurrentBacktester::start_backtest(size_t strategy_index) {
    running_ = true;
    completed_count_ = 0;

    {
        std::lock_guard<std::mutex> lock(cout_mutex_);
        std::cout << "\nStarting backtest for " << instruments_.size() << " instruments\n\n";
    }

    std::vector<std::future<void>> futures;

    for(auto& [id, config] : instruments_) {
        futures.emplace_back(std::async(std::launch::async, [this, &config, strategy_index]() {
            try {
                std::string log_file = config.instrument_id + "_backtest.log";

                {
                    std::lock_guard<std::mutex> lock(cout_mutex_);
                    std::cout << "[" << std::this_thread::get_id() << "] Starting "
                              << config.instrument_id << " backtest...\n";
                }

                config.backtester->create_strategy(strategy_index);
                config.backtester->set_trading_times(config.backtest_file, config.train_file);

                if(strategy_index == 1) {
                    {
                        std::lock_guard<std::mutex> lock(cout_mutex_);
                        std::cout << "[" << std::this_thread::get_id() << "] "
                                  << config.instrument_id << ": Training model...\n";
                    }

                    config.backtester->train_model();

                    {
                        std::lock_guard<std::mutex> lock(cout_mutex_);
                        std::cout << "[" << std::this_thread::get_id() << "] "
                                  << config.instrument_id << ": Training complete\n";
                    }
                }

                config.backtester->start_backtest();

                {
                    std::lock_guard<std::mutex> lock(cout_mutex_);
                    std::cout << "[" << std::this_thread::get_id() << "] "
                              << config.instrument_id << " completed\n";
                }

            } catch(const std::exception& e) {
                std::lock_guard<std::mutex> lock(cout_mutex_);
                std::cerr << "[" << std::this_thread::get_id() << "] Error in "
                          << config.instrument_id << " backtest: " << e.what() << std::endl;
            }
        }));
    }

    for(auto& future : futures) {
        future.wait();
    }

    running_ = false;
}