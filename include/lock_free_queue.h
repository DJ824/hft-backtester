#ifndef LOCK_FREE_QUEUE_H
#define LOCK_FREE_QUEUE_H

#include <atomic>
#include <array>
#include <optional>
#include <type_traits>

template <typename T, size_t SIZE>
class LockFreeQueue {
private:
    struct Node {
        std::atomic<bool> written{false};
        alignas(T) unsigned char storage[sizeof(T)];

        Node() noexcept : written(false) {}
        ~Node() {
            if (written.load()) {
                reinterpret_cast<T*>(storage)->~T();
            }
        }

        Node(const Node&) = delete;
        Node& operator=(const Node&) = delete;
    };

    std::array<Node, SIZE> buffer_;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};

public:
    LockFreeQueue() = default;

    ~LockFreeQueue() {
        size_t current_head = head_.load();
        size_t current_tail = tail_.load();

        while (current_head != current_tail) {
            Node& node = buffer_[current_head];
            if (node.written.load()) {
                // call destructor of original element
                reinterpret_cast<T*>(node.storage)->~T();
                node.written.store(false);
            }
            current_head = (current_head + 1) % SIZE;
        }

        head_.store(0);
        tail_.store(0);
    }


    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;

    template<typename U>
    __attribute__((always_inline)) bool enqueue(U&& item) {

        size_t curr_tail = tail_.load();
        size_t next_tail = (curr_tail + 1) % SIZE;

        // queue is full
        if (next_tail == head_.load()) {
            return false;
        }

        // use placement new to construct a new T object at the location of curr_tail.storage
        // std::forward preserves the value of the arg (lval/rval)
        new (buffer_[curr_tail].storage) T(std::forward<U>(item));
        buffer_[curr_tail].written.store(true);
        tail_.store(next_tail);

        return true;
    }

    __attribute__((always_inline)) std::optional<T> dequeue() {
        size_t curr_head = head_.load();

        // queue is full
        if (curr_head == tail_.load()) {
            return std::nullopt;
        }

        // data not written yet
        if (!buffer_[curr_head].written.load()) {
            return std::nullopt;
        }

        // use std move to produce an rval ref, which is used for move/copy constructor of T
        std::optional<T> result(std::move(*reinterpret_cast<T*>(buffer_[curr_head].storage)));
        // call destructor of T to completely delete the object
        reinterpret_cast<T*>(buffer_[curr_head].storage)->~T();
        buffer_[curr_head].written.store(false);
        head_.store((curr_head + 1) % SIZE);

        return result;
    }

    __attribute__((always_inline))  bool empty() const {
        return head_.load() == tail_.load();
    }

    __attribute__((always_inline)) size_t size() const {
        size_t head = head_.load();
        size_t tail = tail_.load();
        if (tail >= head) {
            return tail - head;
        } else {
            return SIZE - (head - tail);
        }
    }

    __attribute__((always_inline))  size_t capacity() const {
        return SIZE - 1;
    }
};

#endif