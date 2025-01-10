#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <memory>
#include <iostream>
#include <filesystem>
#include "message.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>

class ParserException : public std::runtime_error {
public:
    explicit ParserException(const std::string& msg) : std::runtime_error(msg) {}
};

class Parser {
public:
    explicit Parser(const std::string& file_path);
    ~Parser();

    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;

    Parser(Parser&& other) noexcept;
    Parser& operator=(Parser&& other) noexcept;

    void parse();

    bool validate_file() const;
    std::string get_file_stats() const;

    const std::string& get_file_path() const { return file_path_; }
    size_t get_message_count() const { return message_stream_.size(); }

    std::vector<message> message_stream_;

private:
    std::string file_path_;
    char* mapped_file_;
    size_t file_size_;

    void parse_mapped_data();
    void parse_line(const char* start, const char* end);
    void cleanup();

    bool check_file_format() const;
    bool verify_message_consistency() const;
};