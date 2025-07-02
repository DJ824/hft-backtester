#include <memory>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <iomanip>
#include "../include/concurrent_backtest.h"
#include "parser.cpp"
#include "../include/message.h"

inline std::vector<std::string> get_available_strategies() {
    return {"imbalance_strat", "linear_model_strat"};
}

inline std::vector<std::string> get_available_data_files() {
    std::vector<std::string> files;
    const std::filesystem::path data_path = std::filesystem::current_path()  / ".." / "data";

    if (!std::filesystem::exists(data_path)) {
        throw std::runtime_error("data directory not found: " + data_path.string());
    }

    for (const auto &entry: std::filesystem::directory_iterator(data_path)) {
        if (entry.path().extension() == ".csv") {
            files.push_back(entry.path().filename().string());
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

inline std::vector<std::string> filter_files_by_prefix(const std::vector<std::string> &files,
                                                const std::string &prefix) {
    std::vector<std::string> filtered;
    std::copy_if(files.begin(), files.end(), std::back_inserter(filtered),
                 [&prefix](const std::string &file) {
                     return file.substr(0, 2) == prefix;
                 });
    return filtered;
}

int main() {
    try {
        auto multi_backtest = std::make_unique<ConcurrentBacktester>();

        std::map<std::string, std::pair<std::string, std::string>> instruments = {
                {"es", {"E-mini S&P 500",    "ESU4"}},
                {"nq", {"E-mini NASDAQ-100", "MNQU4"}}
        };

        auto strategies = get_available_strategies();
        std::cout << "available strategies:\n";
        for (size_t i = 0; i < strategies.size(); ++i) {
            std::cout << i << ". " << strategies[i] << "\n";
        }

        size_t strategy_index;
        std::cout << "\nselect strategy (0-" << strategies.size() - 1 << "): ";
        std::cin >> strategy_index;

        if (strategy_index >= strategies.size()) {
            throw std::runtime_error("invalid strategy selection");
        }

        const std::filesystem::path base_path = std::filesystem::current_path()  / ".." / "data";
        auto data_files = get_available_data_files();

        for (const auto &[prefix, info]: instruments) {
            const auto &[name, symbol] = info;
            std::cout << "\nprocess " << name << " (y/n)? ";
            char response;
            std::cin >> response;

            if (response == 'y') {
                auto instrument_files = filter_files_by_prefix(data_files, prefix);

                if (instrument_files.empty()) {
                    std::cout << "no data files found for " << name << "\n";
                    continue;
                }

                std::cout << "\navailable " << name << " files:\n";
                for (size_t i = 0; i < instrument_files.size(); ++i) {
                    std::cout << i + 1 << ". " << instrument_files[i] << "\n";
                }

                size_t backtest_file_idx;
                std::cout << "select backtest file: ";
                std::cin >> backtest_file_idx;

                if (backtest_file_idx < 1 || backtest_file_idx > instrument_files.size()) {
                    throw std::runtime_error("invalid file selection");
                }

                std::string backtest_file = instrument_files[backtest_file_idx - 1];
                std::cout << "parsing backtest data for " << name << "...\n";
                auto data_parser = std::make_unique<Parser>(
                        (base_path / backtest_file).string());
                data_parser->parse();

                std::vector<book_message> train_messages;
                std::string train_file;
                if (strategy_index == 1) {  // linear_model_strat needs training data
                    std::cout << "select training file: ";
                    size_t train_file_idx;
                    std::cin >> train_file_idx;

                    if (train_file_idx < 1 || train_file_idx > instrument_files.size()) {
                        throw std::runtime_error("invalid training file selection");
                    }

                    train_file = instrument_files[train_file_idx - 1];
                    std::cout << "parsing training data for " << name << "...\n";
                    auto train_parser = std::make_unique<Parser>(
                            (base_path / train_file).string());
                    train_parser->parse();
                    train_messages = std::move(train_parser->message_stream_);
                }

                multi_backtest->add_instrument(
                        prefix,
                        std::move(data_parser->message_stream_),
                        std::move(train_messages),
                        backtest_file,
                        train_file
                );
                std::cout << name << " setup complete\n";
            }
        }

        std::cout << "\nstarting backtest...\n";
        multi_backtest->start_backtest(strategy_index);
        std::cout << "backtest complete\n";

        return 0;

    } catch (const std::exception &e) {
        std::cerr << "fatal error: " << e.what() << std::endl;
        return 1;
    }
}


