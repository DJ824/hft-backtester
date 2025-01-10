// database.h
#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <map>
#include <vector>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include "limit.h"
#include "lock_free_queue.h"
#include <condition_variable>
#include <mutex>
#include "message.h"

class DatabaseManager {
private:
    std::string tcp_host_;
    int tcp_port_;
    int sock_;
    struct sockaddr_in serv_addr_;
    std::thread db_thread_;
    std::thread orderbook_thread_;
    std::unique_ptr<LockFreeQueue<std::string, 1000000>> db_queue_;
    std::atomic<bool> stop_threads_{false};

    struct OrderBookUpdate {
        std::vector<std::pair<int32_t, uint64_t>> bids_;
        std::vector<std::pair<int32_t, uint64_t>> offers_;
        uint64_t timestamp_;
    };
    std::unique_ptr<LockFreeQueue<OrderBookUpdate, 1000000>> orderbook_queue_;

    std::condition_variable cv;
    std::mutex mutex;

    void log_error(const std::string& error_message);
    void log_info(const std::string& info_message);
    bool connect_to_server();
    void process_database_queue();
    void process_orderbook_queue();
    std::string send_query(const std::string& query);
    std::vector<message> parse_csv_response(const std::string& response);



public:
    DatabaseManager(std::string host, int port);
    ~DatabaseManager();
    bool test_connection_and_query();

    std::vector<message> read_csv_from_questdb();


    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;
    DatabaseManager(DatabaseManager&& other) noexcept;
    DatabaseManager& operator=(DatabaseManager&& other) noexcept;

    void send_line_protocol_tcp(const std::string &line_protocol);

    template<typename BidCompare, typename AskCompare>
    inline void update_limit_orderbook(const std::map<int32_t, Limit *, BidCompare> &bids,
                                       const std::map<int32_t, Limit *, AskCompare> &offers) {
        OrderBookUpdate update;
        update.timestamp_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

        auto process_side = [](const auto &side, std::vector<std::pair<int32_t, uint64_t>> &update_side) {
            int count = 0;
            for (const auto &[price, limit_ptr] : side) {
                if (++count > 20) break;
                update_side.emplace_back(price, limit_ptr->volume_);
            }
        };

        process_side(bids, update.bids_);
        process_side(offers, update.offers_);

        orderbook_queue_->enqueue(std::move(update));
    }
};