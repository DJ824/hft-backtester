#include "async_logger.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <chrono>
#include <unistd.h>

AsyncLogger::AsyncLogger(Connection* connection,
                         const std::string& csv_file,
                         const std::string& instrument_id,
                         size_t buffer_size)
        : db_connection_(connection)
        , instrument_id_(instrument_id)
        , buffer_size_(buffer_size)
        , buffer_offset_(0) {

    log_fd_ = open(csv_file.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (log_fd_ == -1) {
        throw std::runtime_error("failed to open log file: " + std::string(strerror(errno)));
    }

    if (ftruncate(log_fd_, buffer_size_) == -1) {
        close(log_fd_);
        throw std::runtime_error("failed to resize log file: " + std::string(strerror(errno)));
    }

    log_buffer_ = static_cast<char*>(mmap(nullptr, buffer_size_,
                                          PROT_READ | PROT_WRITE,
                                          MAP_SHARED, log_fd_, 0));
    if (log_buffer_ == MAP_FAILED) {
        close(log_fd_);
        throw std::runtime_error("failed to map log file: " + std::string(strerror(errno)));
    }

    write_to_buffer("timestamp,bid,ask,position,trade_count,pnl,instrument\n");

    console_thread_ = std::thread(&AsyncLogger::console_loop, this);
    csv_thread_ = std::thread(&AsyncLogger::csv_loop, this);
}

AsyncLogger::~AsyncLogger() {
    running_ = false;

    if (console_thread_.joinable()) {
        console_thread_.join();
    }
    if (csv_thread_.joinable()) {
        csv_thread_.join();
    }

    flush_buffer();
    munmap(log_buffer_, buffer_size_);
    close(log_fd_);
}

void AsyncLogger::console_loop() {
    while (running_ || !console_queue_.empty()) {
        std::optional<LogEntry> entry = console_queue_.dequeue();
        if (entry) {
            std::cout << "\033[1;36m" << entry->timestamp << "\033[0m | "
                      << "instrument: " << entry->instrument_id << " | "
                      << "position: " << entry->position << " | "
                      << "bid/ask: " << entry->bid << "/" << entry->ask << " | "
                      << "pnl: " << (entry->pnl >= 0 ? "\033[1;32m" : "\033[1;31m")
                      << std::fixed << std::setprecision(2) << entry->pnl
                      << "\033[0m" << std::endl;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void AsyncLogger::csv_loop() {
    std::string last_flush_time;
    const size_t FLUSH_BATCH_SIZE = 1000;
    size_t entries_since_flush = 0;

    while (running_ || !csv_queue_.empty()) {
        std::optional<LogEntry> entry = csv_queue_.dequeue();
        if (entry) {
            std::string log_line = format_log_entry(*entry);
            write_to_buffer(log_line);

            ++entries_since_flush;
            if (entries_since_flush >= FLUSH_BATCH_SIZE ||
                (last_flush_time != entry->timestamp.substr(0, 17))) {
                flush_buffer();
                entries_since_flush = 0;
                last_flush_time = entry->timestamp.substr(0, 17);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    flush_buffer();
}

void AsyncLogger::write_to_buffer(const std::string& log_line) {
    if (buffer_offset_ + log_line.size() > buffer_size_) {
        flush_buffer();
    }
    memcpy(log_buffer_ + buffer_offset_, log_line.c_str(), log_line.size());
    buffer_offset_ += log_line.size();
}

void AsyncLogger::flush_buffer() {
    if (buffer_offset_ > 0) {
        msync(log_buffer_, buffer_size_, MS_SYNC);
        buffer_offset_ = 0;
    }
}

std::string AsyncLogger::format_log_entry(const LogEntry& entry) {
    std::ostringstream oss;
    oss << entry.timestamp << ","
        << entry.bid << ","
        << entry.ask << ","
        << entry.position << ","
        << entry.trade_count << ","
        << std::fixed << std::setprecision(6) << entry.pnl << ","
        << entry.instrument_id << "\n";
    return oss.str();
}

void AsyncLogger::format_and_send_to_db(const LogEntry& entry) {
    std::stringstream batch_data;
    batch_data << "trading_log_" << entry.instrument_id << " "
               << "bid=" << entry.bid << "i,"
               << "ask=" << entry.ask << "i,"
               << "position=" << entry.position << "i,"
               << "trade_count=" << entry.trade_count << "i,"
               << "pnl=" << std::fixed << std::setprecision(6) << entry.pnl
               << " " << format_timestamp(entry.timestamp) << "\n";

    db_connection_->send_trade_log(batch_data.str());
}

void AsyncLogger::log(const std::string& timestamp,
                      int32_t bid,
                      int32_t ask,
                      int position,
                      int trade_count,
                      float pnl) {
    LogEntry entry{
            timestamp,
            bid,
            ask,
            position,
            trade_count,
            pnl,
            instrument_id_
    };

    console_queue_.enqueue(entry);
    csv_queue_.enqueue(entry);
    format_and_send_to_db(entry);
}

uint64_t AsyncLogger::format_timestamp(const std::string& timestamp) {
    std::tm tm = {};
    std::istringstream ss(timestamp);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");

    tm.tm_hour -= 5;

    auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));

    int milliseconds = 0;
    size_t dot_pos = timestamp.find('.');
    if (dot_pos != std::string::npos) {
        milliseconds = std::stoi(timestamp.substr(dot_pos + 1, 3));
    }

    tp += std::chrono::milliseconds(milliseconds);
    auto duration = tp.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}