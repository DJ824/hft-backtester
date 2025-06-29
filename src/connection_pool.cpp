#include "../include/connection_pool.h"
#include <iostream>

ConnectionPool::ConnectionPool(const std::string& host, int port,
                               size_t initial_size, size_t max_size) :
    host_(host), port_(port), max_size_(max_size),
    initial_size_(initial_size)
{
    for (size_t i = 0; i < initial_size_; ++i)
    {
        if (!add_connection())
        {
            throw std::runtime_error("failed to create pool");
        }
    }
}

bool ConnectionPool::add_connection()
{
    if (current_size_ >= max_size_)
    {
        return false;
    }

    try
    {
        auto conn = std::make_unique<Connection>(
            host_, port_, "conn_" + std::to_string(current_size_));

        std::lock_guard<std::mutex> lock(pool_mutex_);
        connections_.push_back(std::move(conn));
        available_connections_.push(connections_.back().get());
        ++current_size_;
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "failed to create connection: " << e.what() << std::endl;
        return false;
    }
}

Connection* ConnectionPool::acquire_connection()
{
    std::unique_lock<std::mutex> lock(pool_mutex_);

    while (available_connections_.empty() && !shutdown_)
    {
        if (current_size_ < max_size_)
        {
            if (add_connection())
                break;
        }
        pool_cv_.wait_for(lock, std::chrono::seconds(5));
    }

    if (shutdown_ || available_connections_.empty())
    {
        return nullptr;
    }

    auto* conn = available_connections_.front();
    available_connections_.pop();
    conn->set_in_use(true);
    return conn;
}

void ConnectionPool::release_connection(Connection* conn)
{
    if (!conn)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(pool_mutex_);
    conn->set_in_use(false);
    available_connections_.push(conn);
    pool_cv_.notify_one();
}

ConnectionPool::~ConnectionPool()
{
    shutdown_ = true;
    pool_cv_.notify_all();

    std::lock_guard<std::mutex> lock(pool_mutex_);
    connections_.clear();
    while (!available_connections_.empty())
    {
        available_connections_.pop();
    }
}

void ConnectionPool::send_trade_log_pool(const std::string& line_protocol)
{
    auto* conn = acquire_connection();
    if (conn)
    {
        conn->send_trade_log(line_protocol);
        release_connection(conn);
    }
}

/*
void ConnectionPool::send_orderbook_update_pool(const OrderBookUpdate& update) {
    auto* conn = acquire_connection();
    if (conn) {
        conn->send_orderbook_update(update);
        release_connection(conn);
    }
}
*/

size_t ConnectionPool::get_active_connections() const { return current_size_; }

size_t ConnectionPool::get_available_connections()
{
    std::lock_guard<std::mutex> lock(pool_mutex_);
    return available_connections_.size();
}

void ConnectionPool::batch_trade_logs(const std::vector<std::string>& logs)
{
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    log_buffer_.insert(log_buffer_.end(), logs.begin(), logs.end());

    if (log_buffer_.size() >= BATCH_SIZE)
    {
        auto* conn = acquire_connection();
        if (conn)
        {
            std::string batch;
            batch.reserve(log_buffer_.size() * 100);

            for (const auto& log : log_buffer_)
            {
                batch += log + "\n";
            }

            conn->send_trade_log(batch);
            release_connection(conn);
            log_buffer_.clear();
        }
    }
}
