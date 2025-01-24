#include <memory>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <iomanip>
#include "../include/concurrent_backtest.h"
#include "parser.cpp"
#include <databento/historical.hpp>

using namespace databento;

std::vector<std::string> get_available_strategies() {
    return {"imbalance_strat", "linear_model_strat"};
}

std::vector<std::string> get_available_data_files() {
    std::vector<std::string> files;
    const std::filesystem::path data_path = std::filesystem::current_path() / ".." / ".." / "data";

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

std::vector<std::string> filter_files_by_prefix(const std::vector<std::string> &files,
                                                const std::string &prefix) {
    std::vector<std::string> filtered;
    std::copy_if(files.begin(), files.end(), std::back_inserter(filtered),
                 [&prefix](const std::string &file) {
                     return file.substr(0, 2) == prefix;
                 });
    return filtered;
}

message convert_mbo_to_message(const MboMsg &mbo) {
    char action;
    switch (mbo.action) {
        case Action::Add:
            action = 'A';
            break;
        case Action::Cancel:
            action = 'C';
            break;
        case Action::Modify:
            action = 'M';
            break;
        case Action::Trade:
            action = 'T';
            break;
        default:
            action = 'C';
    }

    bool side = (mbo.side == databento::Side::Bid);
    auto price = static_cast<int32_t>(mbo.price);

    uint64_t ts_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
            mbo.hd.ts_event.time_since_epoch()
    ).count();

    return message(
            mbo.order_id,
            ts_nanos,
            static_cast<uint32_t>(mbo.size),
            price,
            action,
            side
    );
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

        std::cout << "\nselect data source:\n";
        std::cout << "1. csv files\n";
        std::cout << "2. databento api\n";
        int data_source;
        std::cin >> data_source;

        const std::filesystem::path base_path = std::filesystem::current_path() / ".." / ".." / "data";

        if (data_source == 1) {
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

                    std::vector<message> train_messages;
                    std::string train_file;
                    if (strategy_index == 1) {
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
                        train_messages = train_parser->message_stream_;
                    }

                    multi_backtest->add_instrument(
                            prefix,
                            data_parser->message_stream_,
                            train_messages,
                            backtest_file,
                            train_file
                    );
                    std::cout << name << " setup complete\n";
                }
            }
        } else if (data_source == 2) {
            std::cout << "enter Databento API key: ";
            std::string api_key;
            std::cin >> api_key;

            std::cout << "enter backtest date (YYYY-MM-DD): ";
            std::string backtest_date;
            std::cin >> backtest_date;

            std::string train_date;
            if (strategy_index == 1) {
                std::cout << "enter training date (YYYY-MM-DD): ";
                std::cin >> train_date;
            }

            for (const auto &[prefix, info]: instruments) {
                const auto &[name, symbol] = info;
                std::cout << "\nprocess " << name << " (y/n)? ";
                char response;
                std::cin >> response;

                if (response == 'y') {
                    auto client = HistoricalBuilder{}
                            .SetKey(api_key)
                            .Build();

                    std::vector<message> backtest_messages;
                    auto process_message = [&backtest_messages](const Record &record) {
                        const auto &mbo_msg = record.Get<MboMsg>();
                        backtest_messages.push_back(convert_mbo_to_message(mbo_msg));
                        return KeepGoing::Continue;
                    };

                    std::cout << "fetching backtest data for " << name << "...\n";
                    client.TimeseriesGetRange(
                            "GLBX.MDP3",
                            {backtest_date + "T09:30", backtest_date + "T16:00"},
                            {symbol},
                            Schema::Mbo,
                            SType::RawSymbol,
                            SType::InstrumentId,
                            {},
                            {},
                            process_message
                    );
                    std::cout << "received " << backtest_messages.size() << " messages for backtest\n";

                    std::vector<message> train_messages;
                    if (strategy_index == 1) {
                        auto train_client = HistoricalBuilder{}
                                .SetKey(api_key)
                                .Build();

                        auto process_train_message = [&train_messages](const Record &record) {
                            const auto &mbo_msg = record.Get<MboMsg>();
                            train_messages.push_back(convert_mbo_to_message(mbo_msg));
                            return KeepGoing::Continue;
                        };

                        std::cout << "fetching training data for " << name << "...\n";
                        train_client.TimeseriesGetRange(
                                "GLBX.MDP3",
                                {train_date + "T09:30", train_date + "T16:00"},
                                {symbol},
                                Schema::Mbo,
                                SType::RawSymbol,
                                SType::InstrumentId,
                                {},
                                {},
                                process_train_message
                        );
                        std::cout << "received " << train_messages.size() << " messages for training\n";
                    }

                    multi_backtest->add_instrument(
                            prefix,
                            backtest_messages,
                            train_messages,
                            backtest_date,
                            train_date
                    );

                    std::cout << name << " setup complete\n";
                }
            }
        } else {
            throw std::runtime_error("invalid data source selection");
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