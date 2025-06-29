#pragma once
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <cstdint>
#include <cstddef>
#include "message.h"

class Orderbook;

class MarketDataIngestor
{
public:
    explicit MarketDataIngestor(std::vector<book_message> messages);
    ~MarketDataIngestor();

    MarketDataIngestor(const MarketDataIngestor&) = delete;
    MarketDataIngestor& operator=(const MarketDataIngestor&) = delete;
    MarketDataIngestor(MarketDataIngestor&&) = delete;
    MarketDataIngestor& operator=(MarketDataIngestor&&) = delete;

    void start();
    void stop();
    bool is_completed() const;
    const Orderbook& get_orderbook() const;

    void print_performance_stats() const;
    uint64_t get_messages_processsed() const;
    size_t get_total_messages() const;
    double get_curr_rate() const;

private:
    std::unique_ptr<Orderbook> orderbook_;
    std::vector<book_message> messages_;

    alignas(64) std::atomic<bool> running_{false};
    alignas(64) std::atomic<bool> completed_{false};
    std::thread injestor_thread_;

    alignas(64) std::atomic<uint64_t> messages_processed_{0};
    alignas(64) std::atomic<uint64_t> start_time_ns_{0};
    alignas(64) std::atomic<uint64_t> end_time_ns_{0};

    void injest_market_data();
    void start_perf_tracking();
    void end_perf_tracking(uint64_t processed_count);
};
