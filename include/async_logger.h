#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <sys/mman.h>
#include <fcntl.h>
#include "db_connection.h"
#include "lock_free_queue.h"

class AsyncLogger {
private:
  struct LogEntry {
    std::string timestamp;
    int32_t bid;
    int32_t ask;
    int position;
    int trade_count;
    float pnl;
    std::string instrument_id;
  };

  std::thread console_thread_;
  std::thread csv_thread_;
  std::atomic<bool> running_{true};

  Connection *db_connection_;
  std::string instrument_id_;

  int log_fd_;
  char *log_buffer_;
  size_t buffer_size_;
  size_t buffer_offset_;
  static constexpr size_t DEFAULT_BUFFER_SIZE = 10 * 1024 * 1024;

  LockFreeQueue<LogEntry, 1000000> console_queue_;
  LockFreeQueue<LogEntry, 1000000> csv_queue_;

  void console_loop();
  void csv_loop();

  uint64_t format_timestamp(const std::string &timestamp);
  void write_to_buffer(const std::string &log_line);
  void flush_buffer();
  static std::string format_log_entry(const LogEntry &entry);
  void format_and_send_to_db(const LogEntry &entry);

public:
  AsyncLogger(Connection *connection,
              const std::string &csv_file,
              const std::string &instrument_id,
              size_t buffer_size = DEFAULT_BUFFER_SIZE);
  ~AsyncLogger();

  AsyncLogger(const AsyncLogger &) = delete;
  AsyncLogger &operator=(const AsyncLogger &) = delete;
  AsyncLogger(AsyncLogger &&) = delete;
  AsyncLogger &operator=(AsyncLogger &&) = delete;

  void log(const std::string &timestamp,
           int32_t bid,
           int32_t ask,
           int position,
           int trade_count,
           float pnl);
};