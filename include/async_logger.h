#pragma once

#include <atomic>
#include <thread>
#include <string>
#include "database.h"
#include "lock_free_queue.h"

class AsyncLogger {
private:
    struct log_entry {
        std::string timestamp;
        int32_t bid;
        int32_t ask;
        int position;
        int trade_count;
        float pnl;
    };

    std::atomic<bool> running_;
    LockFreeQueue<log_entry, 1000000> console_queue_;
    LockFreeQueue<log_entry, 1000000> csv_queue_;
    LockFreeQueue<log_entry, 1000000> db_queue_;
    std::thread console_thread_;
    std::thread csv_thread_;
    std::thread db_thread_;
    int log_fd_;
    char *log_buffer_;
    size_t buffer_size_;
    size_t buffer_offset_;
    DatabaseManager &db_manager_;

    void console_loop();
    void csv_loop();
    void db_loop();
    uint64_t format_timestamp(const std::string &timestamp);
    void write_to_buffer(const std::string &log_line);
    void flush_buffer();
    static std::string format_log_entry(const log_entry &entry);

public:
    AsyncLogger(const std::string &log_file, DatabaseManager &db_manager, size_t buffer_size = 10485760);
    ~AsyncLogger();

    void log(const std::string &timestamp, int32_t bid, int32_t ask, int position, int trade_count, float pnl);
};