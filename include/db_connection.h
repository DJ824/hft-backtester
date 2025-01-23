#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <sys/socket.h>
#include <netinet/in.h>
#include "lock_free_queue.h"

struct OrderBookUpdate {
    std::vector<std::pair<int32_t, uint64_t>> bids_;
    std::vector<std::pair<int32_t, uint64_t>> offers_;
    uint64_t timestamp_;
};


class Connection {
private:
    int sock_{-1};
    struct sockaddr_in serv_addr_{};
    std::string connection_id_;
    std::atomic<bool> active_{false};
    std::atomic<bool> in_use_{false};
    std::unique_ptr<LockFreeQueue<std::string, 1000000>> trade_log_queue_;
    std::thread trade_log_thread_;
    std::atomic<bool> stop_thread_{false};
    std::mutex conn_mutex_;
    void process_database_queue();
    bool ensure_connected();
    bool connect();
    void reconnect();

public:
    Connection(const std::string& host, int port, const std::string& id);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    void send_trade_log(const std::string& log_entry);
    bool is_active() const { return active_; }
    bool is_in_use() const { return in_use_; }
    void set_in_use(bool value) { in_use_ = value; }
    const std::string& get_id() const { return connection_id_; }
};