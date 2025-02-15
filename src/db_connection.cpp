#include "db_connection.h"
#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>

Connection::Connection(const std::string &host, int port, const std::string &id)
        : connection_id_(id), trade_log_queue_(std::make_unique<LockFreeQueue<std::string, 1000000>>()) {

    std::memset(&serv_addr_, 0, sizeof(serv_addr_));
    serv_addr_.sin_family = AF_INET;
    serv_addr_.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &serv_addr_.sin_addr) <= 0) {
        throw std::runtime_error("invalid address: " + host);
    }

    trade_log_thread_ = std::thread(&Connection::process_database_queue, this);
}

Connection::~Connection() {
    stop_thread_ = true;

    if (trade_log_thread_.joinable()) {
        trade_log_thread_.join();
    }

    if (sock_ != -1) {
        ::close(sock_);
    }
}

void Connection::process_database_queue() {
    while (!stop_thread_ || !trade_log_queue_->empty()) {
        std::optional<std::string> line_protocol = trade_log_queue_->dequeue();
        if (line_protocol) {
            if (ensure_connected()) {
                ssize_t sent_bytes = ::send(sock_, line_protocol->c_str(),
                                            line_protocol->length(), 0);
                if (sent_bytes < 0) {
                    std::cerr << connection_id_ << " error sending data: "
                              << strerror(errno) << std::endl;
                    reconnect();
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

bool Connection::ensure_connected() {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    if (sock_ == -1) {
        return connect();
    }
    return true;
}

bool Connection::connect() {
    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ < 0) {
        std::cerr << connection_id_ << " socket creation error: "
                  << strerror(errno) << std::endl;
        return false;
    }

    int opt = 1;
    if (setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        std::cerr << connection_id_ << " error setting socket options: "
                  << strerror(errno) << std::endl;
        close(sock_);
        sock_ = -1;
        return false;
    }

    struct timeval tv{};
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (::connect(sock_, reinterpret_cast<struct sockaddr *>(&serv_addr_),
                  sizeof(serv_addr_)) < 0) {
        std::cerr << connection_id_ << " connection failed: "
                  << strerror(errno) << std::endl;
        close(sock_);
        sock_ = -1;
        return false;
    }

    active_ = true;
    return true;
}

void Connection::reconnect() {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    if (sock_ != -1) {
        ::close(sock_);
        sock_ = -1;
    }
    active_ = false;
}

void Connection::send_trade_log(const std::string &log_entry) { trade_log_queue_->enqueue(log_entry); }