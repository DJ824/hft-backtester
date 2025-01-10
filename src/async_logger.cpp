#include "async_logger.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>

AsyncLogger::AsyncLogger(const std::string &log_file, DatabaseManager &db_manager, size_t buffer_size)
        : running_(true), buffer_size_(buffer_size), buffer_offset_(0), db_manager_(db_manager) {
    log_fd_ = open(log_file.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (log_fd_ == -1) {
        throw std::runtime_error("failed to open log file");
    }
    if (ftruncate(log_fd_, buffer_size_) == -1) {
        throw std::runtime_error("failed to resize log file");
    }
    log_buffer_ = static_cast<char *>(mmap(nullptr, buffer_size_, PROT_READ | PROT_WRITE, MAP_SHARED, log_fd_, 0));
    if (log_buffer_ == MAP_FAILED) {
        throw std::runtime_error("failed to map log file");
    }

    std::string header = "timestamp,bid,ask,position,trade_count,pnl\n";
    write_to_buffer(header);
    std::cout << header;

    //console_thread_ = std::thread(&AsyncLogger::console_loop, this);
    csv_thread_ = std::thread(&AsyncLogger::csv_loop, this);
    db_thread_ = std::thread(&AsyncLogger::db_loop, this);
}

AsyncLogger::~AsyncLogger() {
    running_ = false;
    if (console_thread_.joinable()) {
        console_thread_.join();
    }
    if (csv_thread_.joinable()) {
        csv_thread_.join();
    }
    if (db_thread_.joinable()) {
        db_thread_.join();
    }
    munmap(log_buffer_, buffer_size_);
    close(log_fd_);
}

void AsyncLogger::console_loop() {
    while (running_ || !console_queue_.empty()) {
        std::optional<log_entry> entry = console_queue_.dequeue();
        if (entry) {
            std::string log_line = format_log_entry(*entry);
            std::cout << log_line;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void AsyncLogger::csv_loop() {
    while (running_ || !csv_queue_.empty()) {
        std::optional<log_entry> entry = csv_queue_.dequeue();
        if (entry) {
            std::string log_line = format_log_entry(*entry);
            write_to_buffer(log_line);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    flush_buffer();
}

void AsyncLogger::db_loop() {
    while (running_ || !db_queue_.empty()) {
        std::optional<log_entry> entry = db_queue_.dequeue();
        if (entry) {
            uint64_t formatted_timestamp = format_timestamp(entry->timestamp);
            std::stringstream line_protocol;
            line_protocol << "trading_log "
                          << "bid=" << entry->bid << "i,"
                          << "ask=" << entry->ask << "i,"
                          << "position=" << entry->position << "i,"
                          << "trade_count=" << entry->trade_count << "i,"
                          << "pnl=" << std::fixed << std::setprecision(6) << entry->pnl
                          << " " << formatted_timestamp;
            db_manager_.send_line_protocol_tcp(line_protocol.str() + "\n");
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

uint64_t AsyncLogger::format_timestamp(const std::string &timestamp) {
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

void AsyncLogger::write_to_buffer(const std::string &log_line) {
    if (buffer_offset_ + log_line.size() > buffer_size_) {
        flush_buffer();
    }
    memcpy(log_buffer_ + buffer_offset_, log_line.c_str(), log_line.size());
    buffer_offset_ += log_line.size();
}

void AsyncLogger::flush_buffer() {
    msync(log_buffer_, buffer_size_, MS_SYNC);
    buffer_offset_ = 0;
}

std::string AsyncLogger::format_log_entry(const log_entry &entry) {
    std::ostringstream oss;
    oss << entry.timestamp << ","
        << std::fixed << std::setprecision(6)
        << entry.bid << ","
        << entry.ask << ","
        << entry.position << ","
        << entry.trade_count << ","
        << entry.pnl << "\n";
    return oss.str();
}

void AsyncLogger::log(const std::string &timestamp, int32_t bid, int32_t ask, int position, int trade_count, float pnl) {
    log_entry entry{timestamp, bid, ask, position, trade_count, pnl};
   // console_queue_.enqueue(entry);
    csv_queue_.enqueue(entry);
    db_queue_.enqueue(entry);
}