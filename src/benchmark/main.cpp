#include "../../include/market_data_ingestor.h"
#include "../parser.cpp"
#include <filesystem>
#include <iostream>
#include <memory>

int main() {
  try {
    const std::filesystem::path data_file =
        std::filesystem::current_path() / ".." / ".." / "data" / "es0801.csv";

    std::cout << "Parsing " << data_file << "..." << std::endl;
    auto parser = std::make_unique<Parser>(data_file.string());
    parser->parse();
    int32_t min_px = INT32_MAX, max_px = INT32_MIN;

    std::cout << "Creating ingester with " << parser->message_stream_.size()
              << " messages..." << std::endl;

    MarketDataIngestor ingester(std::move(parser->message_stream_));
    ingester.start();

    while (!ingester.is_completed()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ingester.stop();
    ingester.print_performance_stats();

    return 0;

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}
