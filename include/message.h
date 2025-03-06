
#ifndef DATABENTO_ORDERBOOK_MESSAGE_H
#define DATABENTO_ORDERBOOK_MESSAGE_H
#include <cstdint>
struct book_message {
    uint64_t id_;
    uint64_t time_;
    uint32_t size_;
    int32_t price_;
    char action_;
    bool side_;

    book_message(uint64_t id, uint64_t time, uint32_t size, int32_t price, char action, bool side)
            : id_(id), time_(time), size_(size), price_(price), action_(action), side_(side) {}
};

#endif //DATABENTO_ORDERBOOK_MESSAGE_H
