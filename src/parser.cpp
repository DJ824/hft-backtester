#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <memory>
#include <iostream>
#include <filesystem>
#include "../include/message.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>

class ParserException : public std::runtime_error {
public:
  explicit ParserException(const std::string &msg) :
    std::runtime_error(msg) {
  }
};

class Parser {
private:
  std::string file_path_;
  char *mapped_file_;
  size_t file_size_;

  void parse_mapped_data() {
    char *current = mapped_file_;
    char *end = mapped_file_ + file_size_;

    for (int i = 0; i < 2 && current < end; ++i) {
      current = static_cast<char *>(memchr(current, '\n', end - current));
      if (current)
        ++current;
      else
        throw ParserException("invalid file format: missing header");
    }

    while (current < end) {
      char *line_end = static_cast<char *>(
        memchr(current, '\n', end - current));
      if (!line_end)
        line_end = end;

      parse_line(current, line_end);
      current = line_end + 1;
    }
  }

  void parse_line(const char *start, const char *end) {
    uint64_t ts_event, order_id;
    int32_t price;
    uint32_t size;
    char action, side;

    const char *token_start = start;
    const char *token_end;

    token_end = strchr(token_start, ',');
    ts_event = strtoull(token_start, nullptr, 10);
    token_start = token_end + 1;

    action = *token_start;
    token_start = strchr(token_start, ',') + 1;

    side = *token_start;
    token_start = strchr(token_start, ',') + 1;

    token_end = strchr(token_start, ',');
    price = strtol(token_start, nullptr, 10);
    token_start = token_end + 1;

    token_end = strchr(token_start, ',');
    size = strtoul(token_start, nullptr, 10);
    token_start = token_end + 1;

    order_id = strtoull(token_start, nullptr, 10);

    bool bid_or_ask = (side == 'B');
    message_stream_.emplace_back(order_id, ts_event, size, price, action,
                                 bid_or_ask);
  }

  void cleanup() {
    if (mapped_file_) {
      munmap(mapped_file_, file_size_);
      mapped_file_ = nullptr;
    }
  }

  [[nodiscard]] bool check_file_format() const { return true; }

  [[nodiscard]] bool verify_message_consistency() const { return true; }

public:
  std::vector<book_message> message_stream_;

  explicit Parser(const std::string &file_path) :
    file_path_(file_path), mapped_file_(nullptr), file_size_(0) {

    if (!std::filesystem::exists(file_path)) {
      throw ParserException("file does not exist: " + file_path);
    }

    message_stream_.reserve(9000000);
  }

  ~Parser() { cleanup(); }

  Parser(Parser &&other) noexcept :
    file_path_(std::move(other.file_path_))
    , mapped_file_(other.mapped_file_)
    , file_size_(other.file_size_)
    , message_stream_(std::move(other.message_stream_)) {

    other.mapped_file_ = nullptr;
    other.file_size_ = 0;
  }

  Parser &operator=(Parser &&other) noexcept {
    if (this != &other) {
      cleanup();

      file_path_ = std::move(other.file_path_);
      mapped_file_ = other.mapped_file_;
      file_size_ = other.file_size_;
      message_stream_ = std::move(other.message_stream_);

      other.mapped_file_ = nullptr;
      other.file_size_ = 0;
    }
    return *this;
  }

  Parser(const Parser &) = delete;
  Parser &operator=(const Parser &) = delete;

  void parse() {
    std::cout << "parsing messages" << std::endl;

    int fd = open(file_path_.c_str(), O_RDONLY);
    if (fd == -1) {
      throw ParserException("failed to open file: " + file_path_);
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
      close(fd);
      throw ParserException("failed to get file stats");
    }

    file_size_ = sb.st_size;

    mapped_file_ = static_cast<char *>(mmap(nullptr, file_size_, PROT_READ,
                                            MAP_PRIVATE, fd, 0));
    close(fd);

    if (mapped_file_ == MAP_FAILED) {
      mapped_file_ = nullptr;
      throw ParserException("failed to memory map file");
    }

    try {
      parse_mapped_data();
    } catch (const std::exception &e) {
      cleanup();
      throw;
    }

    std::cout << "finished parsing" << std::endl;
  }

  bool validate_file() const {
    return check_file_format() && verify_message_consistency();
  }

  const std::string &get_file_path() const { return file_path_; }

  size_t get_message_count() const { return message_stream_.size(); }
};