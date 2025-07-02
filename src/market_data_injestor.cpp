#include "../include/market_data_ingestor.h"
#include "book/orderbook.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <pthread.h>

using SteadyClock = std::chrono::steady_clock;

MarketDataIngestor::MarketDataIngestor(std::vector<book_message> msgs) {
  int32_t min_px = INT32_MAX, max_px = INT32_MIN;
  for (const auto &m : msgs) {
    if (m.price_ < 99 || m.price_ > 10'000'00) {
      continue;
    }
    min_px = std::min(min_px, m.price_);
    max_px = std::max(max_px, m.price_);
  }
  std::cout << "price window: " << min_px << " … " << max_px << '\n';
  orderbook_ = std::make_unique<Orderbook>(min_px, max_px);
  messages_ = std::move(msgs);
}

MarketDataIngestor::~MarketDataIngestor() { stop(); }

void MarketDataIngestor::start() {
  if (running_.load()) {
    return;
  }
  std::cout << "starting market data injestor\n";
  running_.store(true);
  completed_.store(false);
  unsigned cpu = 3;
  injestor_thread_ = std::thread([this, cpu] {
    pin_cpu(cpu);
    ingest_market_data();
  });
}

void MarketDataIngestor::stop() {
  if (!running_.load() && !injestor_thread_.joinable()) {
    return;
  }
  std::cout << "stopping market data injestion\n";
  running_.store(false);
  if (injestor_thread_.joinable()) {
    injestor_thread_.join();
  }
}

void MarketDataIngestor::start_perf_tracking() {
  auto ts = SteadyClock::now().time_since_epoch();
  start_time_ns_.store(
      std::chrono::duration_cast<std::chrono::nanoseconds>(ts).count());
  messages_processed_.store(0);
}

void MarketDataIngestor::end_perf_tracking(uint64_t processed) {
  auto ts = SteadyClock::now().time_since_epoch();
  end_time_ns_.store(
      std::chrono::duration_cast<std::chrono::nanoseconds>(ts).count());
  messages_processed_.store(processed);
  completed_.store(true);
}

double MarketDataIngestor::get_curr_rate() const {
  uint64_t start_ns = start_time_ns_.load(std::memory_order_relaxed);
  uint64_t processed = messages_processed_.load(std::memory_order_relaxed);
  if (start_ns == 0 || processed == 0) {
    return 0.0;
  }
  uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        SteadyClock::now().time_since_epoch())
                        .count();
  double elapsed_ns = static_cast<double>(now_ns - start_ns);
  return processed * 1e9 / elapsed_ns;
}

void MarketDataIngestor::print_performance_stats() const {
  uint64_t processed = messages_processed_.load();
  uint64_t start_ns = start_time_ns_.load();
  uint64_t end_ns = end_time_ns_.load();

  double elapsed_ms = (end_ns - start_ns) / 1'000'000.0;
  double msg_per_sec = processed * 1'000.0 / elapsed_ms;
  double ns_per_msg = (end_ns - start_ns) / static_cast<double>(processed);

  std::cout << "Messages processed: " << processed << '\n'
            << "Time elapsed: " << std::fixed << std::setprecision(2)
            << elapsed_ms << " ms\n"
            << "Messages/second: " << std::setprecision(0) << msg_per_sec
            << '\n'
            << "Nanoseconds/message: " << std::setprecision(1) << ns_per_msg
            << '\n';
}

void MarketDataIngestor::ingest_market_data() {
  std::cout << "market data ingestion thread started\n";
  start_perf_tracking();

  uint64_t processed = 0;
  for (const auto &msg : messages_) {
    if (!running_.load(std::memory_order_relaxed)) {
      break;
    }
    orderbook_->process_msg(msg);
    ++processed;
    // if (processed == 1'000'000)
    // {
    //     orderbook_->print_top_levels(10);
    //     // std::cout << orderbook_->best_ask() << std::endl;
    //     // std::cout << orderbook_->get_best_ask_index() << std::endl;
    //     // std::cout << orderbook_->best_bid() << std::endl;
    //     // std::cout << orderbook_->get_best_bid_index() << std::endl;
    // }
     //std::cout << orderbook_->get_formatted_time_fast() << std::endl;
  }
  end_perf_tracking(processed);
  std::cout << "market data injestion completed\n";
}

void MarketDataIngestor::pin_cpu(unsigned cpu_id) {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(cpu_id, &mask);

  int rc = pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask);
  if (rc != 0) {
    std::cerr << "set affinity failed\n";
  }
  std::cout << "pinned to core " << cpu_id << '\n';
}

bool MarketDataIngestor::is_completed() const { return completed_.load(); }
size_t MarketDataIngestor::get_total_messages() const {
  return messages_.size();
}
uint64_t MarketDataIngestor::get_messages_processsed() const {
  return messages_processed_.load();
}
const Orderbook &MarketDataIngestor::get_orderbook() const {
  return *orderbook_;
}
