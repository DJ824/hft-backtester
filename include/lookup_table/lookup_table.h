#ifndef DATABENTO_ORDERBOOK_LOOKUP_TABLE_H
#define DATABENTO_ORDERBOOK_LOOKUP_TABLE_H

#include <vector>
#include <xxhash/xxhash.h>
#include "order.h"

class OpenAddressTable {
private:
    struct alignas(16) Entry {
        uint64_t key_;
        Order* val_;
        uint16_t probe_dist_;
        uint8_t status_; // 0 for empty, 2 for filled
        uint8_t padding_[5]; // Ensure proper alignment

        Entry() noexcept : key_(0), val_(nullptr), probe_dist_(0), status_(0) {}

        Entry(uint64_t key, Order* val, uint16_t probe_dist, uint8_t status) noexcept :
                key_(key),
                val_(val),
                probe_dist_(probe_dist),
                status_(status) {}

        Entry(const Entry& other) = delete;
        Entry& operator=(const Entry& other) = delete;

        Entry(Entry&& other) noexcept :
                key_(other.key_),
                val_(other.val_),
                probe_dist_(other.probe_dist_),
                status_(other.status_) {
            other.val_ = nullptr;
        }

        Entry& operator=(Entry&& other) noexcept {
            if (this != &other) {
                key_ = other.key_;
                val_ = other.val_;
                probe_dist_ = other.probe_dist_;
                status_ = other.status_;
                other.val_ = nullptr;
            }
            return *this;
        }
    } __attribute__((packed));

    alignas(64) std::vector<Entry> data_;
    size_t size_;

    static constexpr size_t CACHE_LINE_SIZE = 64;
    static constexpr size_t ENTRIES_PER_CACHE_LINE = 4;
    static constexpr double LOAD_FACTOR_THRESHOLD = 0.75;
    static constexpr size_t PREFETCH_DISTANCE = 4;

public:
    explicit OpenAddressTable(size_t initial_size = 64) : size_(0) {
        size_t actual_size = 1;
        while (actual_size < initial_size) actual_size *= 2;
        data_.resize(actual_size);
    }

    static size_t hash_key(uint64_t key) {
        return XXH64(&key, sizeof(key), 0);
    }

    __attribute__((always_inline))
    bool insert(uint64_t key, Order* val) {
        if (load_factor() >= LOAD_FACTOR_THRESHOLD) {
            resize();
        }

        const size_t mask = data_.size() - 1;
        size_t pos = hash_key(key) & mask;
        uint16_t probe_dist = 0;

        while (true) {
            if (data_[pos].status_ == 0 || data_[pos].status_ == 1) {
                data_[pos].key_ = key;
                data_[pos].val_ = val;
                data_[pos].probe_dist_ = static_cast<uint16_t>(probe_dist);
                data_[pos].status_ = 2;
                ++size_;
                return true;
            }

            if (data_[pos].status_ == 2 && data_[pos].key_ == key) {
                data_[pos].val_ = val;
                return true;
            }

            if (probe_dist > data_[pos].probe_dist_) {
                std::swap(key, data_[pos].key_);
                std::swap(val, data_[pos].val_);
                std::swap(probe_dist, data_[pos].probe_dist_);
                data_[pos].status_ = 2;
            }

            pos = next_probe_position(pos);
            ++probe_dist;
        }
    }

    __attribute__((always_inline))
    bool erase(uint64_t key) {
        if (data_.empty()) return false;

        const size_t mask = data_.size() - 1;
        size_t pos = hash_key(key) & mask;
        size_t probe_dist = 0;

        while (true) {
            if (data_[pos].status_ == 0) return false;

            if (data_[pos].status_ == 2 && data_[pos].key_ == key) {
                // Backward shift deletion
                size_t curr = pos;
                while (true) {
                    size_t next = next_probe_position(curr);
                    if (data_[next].status_ != 2 || data_[next].probe_dist_ == 0) {
                        data_[curr].status_ = 0;
                        data_[curr].val_ = nullptr;
                        break;
                    }
                    data_[curr] = std::move(data_[next]);
                    data_[curr].probe_dist_--;
                    curr = next;
                }
                --size_;
                return true;
            }

            if (probe_dist > data_[pos].probe_dist_) return false;

            pos = next_probe_position(pos);
            ++probe_dist;
        }
    }

    __attribute__((always_inline))
    const Order* const * find(uint64_t key) const {
        if (data_.empty()) return nullptr;

        const size_t mask = data_.size() - 1;
        size_t pos = hash_key(key) & mask;
        size_t probe_dist = 0;

        __builtin_prefetch(&data_[pos + ENTRIES_PER_CACHE_LINE], 0, 3);

        while (true) {
            if (data_[pos].status_ == 2) {
                if (data_[pos].key_ == key) {
                    return &data_[pos].val_;
                }
                if (probe_dist > data_[pos].probe_dist_) {
                    return nullptr;
                }
            } else if (data_[pos].status_ == 0) {
                return nullptr;
            }

            pos = next_probe_position(pos);
            ++probe_dist;
        }
    }

    __attribute__((always_inline))
    Order** find(uint64_t key) {
        return const_cast<Order**>(const_cast<const OpenAddressTable*>(this)->find(key));
    }

private:
    __attribute__((always_inline))
    size_t next_probe_position(size_t current_pos) const {
        size_t next_pos = (current_pos + 1) & (data_.size() - 1);

        if (next_pos % ENTRIES_PER_CACHE_LINE == 0) {
            for (size_t i = 1; i <= PREFETCH_DISTANCE; ++i) {
                size_t prefetch_pos = (next_pos + i * CACHE_LINE_SIZE) & (data_.size() - 1);
                __builtin_prefetch(&data_[prefetch_pos], 0, 3);
            }
        }

        return next_pos;
    }

    void resize() {
        size_t new_size = data_.size() * 2;
        std::vector<Entry> new_data(new_size);

        for (const auto& entry : data_) {
            if (entry.status_ == 2) {
                size_t pos = hash_key(entry.key_) & (new_size - 1);
                size_t probe_dist = 0;

                while (true) {
                    if (new_data[pos].status_ == 0) {
                        new_data[pos] = Entry{entry.key_, entry.val_,
                                              static_cast<uint16_t>(probe_dist), 2};
                        break;
                    }

                    if (probe_dist > new_data[pos].probe_dist_) {
                        Entry tmp(std::move(new_data[pos]));
                        new_data[pos] = Entry{entry.key_, entry.val_,
                                              static_cast<uint16_t>(probe_dist), 2};
                        tmp.probe_dist_ = static_cast<uint16_t>(probe_dist + 1);
                        pos = next_probe_position(pos);
                        continue;
                    }

                    pos = next_probe_position(pos);
                    ++probe_dist;
                }
            }
        }

        data_ = std::move(new_data);
    }

public:
    __attribute__((always_inline))
    size_t size() const { return size_; }

    __attribute__((always_inline))
    bool empty() const { return size_ == 0; }

    __attribute__((always_inline))
    size_t capacity() const { return data_.size(); }

    __attribute__((always_inline))
    double load_factor() const {
        return static_cast<double>(size_) / data_.size();
    }

    void clear() {
        data_.clear();
        data_.resize(64);
        size_ = 0;
    }

    void reserve(size_t n) {
        size_t target_size = 1;
        while (target_size < n) target_size *= 2;

        if (target_size > data_.size()) {
            std::vector<Entry> new_data(target_size);

            for (auto& entry : data_) {
                if (entry.status_ == 2) {
                    size_t pos = hash_key(entry.key_) & (target_size - 1);
                    size_t probe_dist = 0;

                    while (true) {
                        if (new_data[pos].status_ == 0) {
                            new_data[pos] = Entry{entry.key_, entry.val_,
                                                  static_cast<uint16_t>(probe_dist), 2};
                            break;
                        }
                        pos = next_probe_position(pos);
                        ++probe_dist;
                    }
                }
            }
            data_ = std::move(new_data);
        }
    }
};

#endif //DATABENTO_ORDERBOOK_LOOKUP_TABLE_H