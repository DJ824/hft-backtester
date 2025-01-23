#pragma once
#include <vector>
#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include "db_connection.h"



class ConnectionPool {
private:
    std::vector<std::unique_ptr<Connection>> connections_;
    std::queue<Connection*> available_connections_;
    std::mutex pool_mutex_;
    std::condition_variable pool_cv_;
    std::string host_;
    int port_;
    size_t max_size_;
    size_t initial_size_;
    std::atomic<size_t> current_size_{0};
    std::atomic<bool> shutdown_{false};

    std::mutex buffer_mutex_;
    std::vector<std::string> log_buffer_;
    static constexpr size_t BATCH_SIZE = 1000;

    bool add_connection();

public:
    ConnectionPool(const std::string& host, int port,
                   size_t initial_size = 4, size_t max_size = 16);
    ~ConnectionPool();

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    Connection* acquire_connection();
    void release_connection(Connection* conn);

    void send_trade_log_pool(const std::string& line_protocol);
    void send_orderbook_update_pool(const OrderBookUpdate& update);
    void batch_trade_logs(const std::vector<std::string>& logs);

    size_t get_active_connections() const;
    size_t get_available_connections();
};