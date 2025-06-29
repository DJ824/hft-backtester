#include "xxhash.h"
#include <optional>
#include <vector>
#include <type_traits>
#include <algorithm>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <stdexcept>

template <typename T>
struct HashTraits
{
    static size_t hash(const T& key)
    {
        if constexpr (std::is_integral_v<T>)
        {
            return XXH64(&key, sizeof(key), 0);
        }
        else
        {
            return std::hash<T>{}(key);
        }
    }
};

class MemoryArena
{
private:
    char* arena_base_;
    size_t arena_size_;
    size_t used_;

public:
    // allocate 1gb
    explicit MemoryArena(size_t arena_size = 4ULL << 30) :
        arena_size_(arena_size), used_(0)
    {

        // first try to allocate huge pages
        arena_base_ = static_cast<char*>(mmap(
            nullptr,
            arena_size_,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
            -1, 0
            ));

        // regular pages if fails
        if (arena_base_ == MAP_FAILED)
        {
            arena_base_ = static_cast<char*>(mmap(
                nullptr,
                arena_size_,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS,
                -1, 0
                ));

            if (arena_base_ == MAP_FAILED)
            {
                throw std::runtime_error("Failed to allocate memory arena");
            }
        }

        madvise(arena_base_, arena_size_, MADV_RANDOM);
    }

    ~MemoryArena()
    {
        if (arena_base_ != MAP_FAILED)
        {
            munmap(arena_base_, arena_size_);
        }
    }

    MemoryArena(const MemoryArena&) = delete;
    MemoryArena& operator=(const MemoryArena&) = delete;

    MemoryArena(MemoryArena&& other) noexcept :
        arena_base_(other.arena_base_), arena_size_(other.arena_size_), used_(other.used_)
    {
    }

    template <typename T>
    T* allocate(size_t count)
    {
        size_t bytes = count * sizeof(T);
        size_t aligned_bytes = align_to_cacheline(bytes);
        if (used_ + aligned_bytes > arena_size_)
        {
            throw std::runtime_error("too big");
        }
        T* result = reinterpret_cast<T*>(arena_base_ + used_);
        used_ += aligned_bytes;
        return result;
    }

    template <typename T>
    T* allocate_at_offset(size_t offset, size_t count)
    {
        size_t bytes = count * sizeof(T);
        size_t alignedBytes = align_to_cacheline(bytes);
        if (offset + alignedBytes > arena_size_)
            throw std::runtime_error("out of bounds");

        used_ = std::max(used_, offset + alignedBytes);

        return reinterpret_cast<T*>(arena_base_ + offset);
    }

    size_t get_used() const { return used_; }
    size_t get_capacity() const { return arena_size_; }
    char* get_base() const { return arena_base_; }

private:
    static size_t align_to_cacheline(size_t bytes)
    {
        constexpr size_t CACHELINE_SIZE = 64;
        return (bytes + CACHELINE_SIZE - 1) & ~(CACHELINE_SIZE - 1);
    }
};

template <typename Key, typename Value, typename Hash = HashTraits<Key>>
class OpenAddressTable
{
public:
    struct MetaDataEntry
    {
        Key key_;
        uint16_t probe_dist_;
        uint8_t status_;
        uint8_t padding_;

        MetaDataEntry() :
            key_{}, probe_dist_(0), status_(0), padding_(0)
        {
        }

        MetaDataEntry(Key k, uint16_t dist, uint8_t stat) :
            key_(k), probe_dist_(dist), status_(stat), padding_(0)
        {
        }
    };

private:
    MemoryArena arena_;
    MetaDataEntry* metadata_;
    Value* values_;
    size_t capacity_;
    size_t size_;
    size_t metadata_offset_;
    size_t values_offset_;

    static constexpr double LOAD_FACTOR_THRESHOLD = 0.85;
    static constexpr size_t INITIAL_CAPACITY = 64;

    void initialize_metadata()
    {
        // Use placement new with default constructor instead of memset
        for (size_t i = 0; i < capacity_; ++i)
        {
            new(&metadata_[i]) MetaDataEntry{};
        }
    }

    void clear_metadata_entry(size_t pos)
    {
        // Use assignment instead of memset for non-trivial types
        metadata_[pos] = MetaDataEntry{};
    }

public:
    explicit OpenAddressTable(size_t arena_size = 4ULL << 30) // 4GB arena
        :
        arena_(arena_size), capacity_(INITIAL_CAPACITY), size_(0)
    {

        metadata_offset_ = 0;
        values_offset_ = align_to_cacheline(capacity_ * sizeof(MetaDataEntry));

        // intial allocation inside the arena
        metadata_ = arena_.allocate_at_offset<MetaDataEntry>(metadata_offset_, capacity_);
        values_ = arena_.allocate_at_offset<Value>(values_offset_, capacity_);

        // Proper initialization instead of memset
        initialize_metadata();

        size_t total_used = values_offset_ + capacity_ * sizeof(Value);
        while (arena_.get_used() < total_used)
        {
            arena_.allocate<char>(64);
        }
    }

    static size_t next_pow2(size_t x)
    {
        if (x <= 2)
            return 2;
        --x;
        x |= x >> 1;
        x |= x >> 2;
        x |= x >> 4;
        x |= x >> 8;
        x |= x >> 16;
        x |= x >> 32;
        return x + 1;
    }

    static size_t align_to_cacheline(size_t bytes)
    {
        constexpr size_t CACHE_LINE_SIZE = 64;
        return (bytes + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);
    }

    bool insert(const Key& key, const Value& value)
    {
        if (load_factor() >= LOAD_FACTOR_THRESHOLD)
        {
            resize();
        }

        const size_t mask = capacity_ - 1;
        size_t pos = Hash::hash(key) & mask;
        uint16_t probe_dist = 0;

        Key working_key = key;
        Value working_value = value;

        while (true)
        {
            MetaDataEntry& meta = metadata_[pos];

            if (meta.status_ == 0)
            {
                meta.key_ = working_key;
                meta.probe_dist_ = probe_dist;
                meta.status_ = 2;
                values_[pos] = std::move(working_value);
                ++size_;
                return true;
            }
            if (meta.status_ == 2 && meta.key_ == key)
            {
                values_[pos] = value;
                return true;
            }
            if (probe_dist > meta.probe_dist_)
            {
                std::swap(working_key, meta.key_);
                std::swap(working_value, values_[pos]);
                std::swap(probe_dist, meta.probe_dist_);
            }
            pos = (pos + 1) & mask;
            ++probe_dist;
        }
    }

    std::optional<Value> get(const Key& key) const
    {
        if (capacity_ == 0)
        {
            return std::nullopt;
        }

        const size_t mask = capacity_ - 1;
        size_t pos = Hash::hash(key) & mask;
        uint16_t probe_dist = 0;

        while (true)
        {
            const MetaDataEntry& meta = metadata_[pos];

            if (meta.status_ == 0)
            {
                return std::nullopt;
            }

            if (meta.status_ == 2 && meta.key_ == key)
            {
                return values_[pos];
            }
            if (probe_dist > meta.probe_dist_)
            {
                return std::nullopt;
            }
            pos = (pos + 1) & mask;
            ++probe_dist;
        }
    }

    // Find method that returns a pointer to the value (for orderbook compatibility)
    Value* find(const Key& key)
    {
        if (capacity_ == 0)
        {
            return nullptr;
        }

        const size_t mask = capacity_ - 1;
        size_t pos = Hash::hash(key) & mask;
        uint16_t probe_dist = 0;

        while (true)
        {
            const MetaDataEntry& meta = metadata_[pos];

            if (meta.status_ == 0)
            {
                return nullptr;
            }

            if (meta.status_ == 2 && meta.key_ == key)
            {
                return &values_[pos];
            }

            if (probe_dist > meta.probe_dist_)
            {
                return nullptr;
            }

            pos = (pos + 1) & mask;
            ++probe_dist;
        }
    }

    bool erase(const Key& key)
    {
        if (capacity_ == 0)
        {
            return false;
        }

        const size_t mask = capacity_ - 1;
        size_t pos = Hash::hash(key) & mask;
        uint16_t probe_dist = 0;

        while (true)
        {
            MetaDataEntry& meta = metadata_[pos];
            if (meta.status_ == 0)
            {
                return false;
            }

            if (meta.status_ == 2 && meta.key_ == key)
            {
                size_t curr_pos = pos;

                while (true)
                {
                    size_t next_pos = (curr_pos + 1) & mask;
                    MetaDataEntry& next_meta = metadata_[next_pos];

                    if (next_meta.status_ != 2 || next_meta.probe_dist_ == 0)
                    {
                        // Use proper cleanup instead of memset
                        clear_metadata_entry(curr_pos);
                        break;
                    }

                    metadata_[curr_pos] = next_meta;
                    values_[curr_pos] = std::move(values_[next_pos]);
                    --metadata_[curr_pos].probe_dist_;
                    curr_pos = next_pos;
                }

                --size_;
                return true;
            }

            if (probe_dist > meta.probe_dist_)
            {
                return false;
            }

            pos = (pos + 1) & mask;
            ++probe_dist;
        }
    }

    // Reserve method - for interface compatibility
    void reserve(size_t hint_capacity)
    {
        // In arena-based allocation, this is essentially a no-op
        // since we pre-allocate a large arena. You could potentially
        // resize to the hint if it's larger than current capacity.
        if (hint_capacity > capacity_)
        {
            // Could trigger a resize to accommodate the hint
            // For now, we'll make it a no-op since arena handles growth
        }
    }

    // Clear method - reset the hash table
    void clear()
    {
        // Properly clear metadata entries
        for (size_t i = 0; i < capacity_; ++i)
        {
            clear_metadata_entry(i);
        }

        // Reset size
        size_ = 0;

        // Note: We don't reset the arena since it's designed for reuse
        // The arena will handle memory management internally
    }

private:
    void resize()
    {
        const size_t old_capacity = capacity_;
        const size_t new_capacity = next_pow2(capacity_ * 2);
        size_t current_end = values_offset_ + old_capacity * sizeof(Value);
        current_end = align_to_cacheline(current_end);
        size_t new_metadata_offset = current_end;
        size_t new_values_offset = align_to_cacheline(
            new_metadata_offset + new_capacity * sizeof(MetaDataEntry)
            );

        MetaDataEntry* new_metadata = arena_.allocate_at_offset<MetaDataEntry>(
            new_metadata_offset, new_capacity
            );
        Value* new_values = arena_.allocate_at_offset<Value>(
            new_values_offset, new_capacity
            );

        // Properly initialize new metadata
        for (size_t i = 0; i < new_capacity; ++i)
        {
            new(&new_metadata[i]) MetaDataEntry{};
        }

        MetaDataEntry* old_metadata = metadata_;
        Value* old_values = values_;

        metadata_ = new_metadata;
        values_ = new_values;
        metadata_offset_ = new_metadata_offset;
        values_offset_ = new_values_offset;
        capacity_ = new_capacity;
        size_t old_size = size_;
        size_ = 0;

        // Rehash existing entries
        for (size_t i = 0; i < old_capacity; ++i)
        {
            if (old_metadata[i].status_ == 2)
            {
                insert_during_resize(old_metadata[i].key_, std::move(old_values[i]));
            }
        }

        size_t total_used = new_values_offset + new_capacity * sizeof(Value);
        while (arena_.get_used() < total_used)
        {
            arena_.allocate<char>(64);
        }
    }

    void insert_during_resize(const Key& key, Value&& val)
    {
        const size_t mask = capacity_ - 1;
        size_t pos = Hash::hash(key) & mask;
        uint16_t probe_dist = 0;

        Key working_key = key;
        Value working_value = std::move(val);

        while (true)
        {
            MetaDataEntry& meta = metadata_[pos];

            if (meta.status_ == 0)
            {
                meta.key_ = working_key;
                meta.probe_dist_ = probe_dist;
                meta.status_ = 2;
                values_[pos] = std::move(working_value);
                ++size_;
                return;
            }

            if (probe_dist > meta.probe_dist_)
            {
                std::swap(working_key, meta.key_);
                std::swap(working_value, values_[pos]);
                std::swap(probe_dist, meta.probe_dist_);
            }

            pos = (pos + 1) & mask;
            ++probe_dist;
        }
    }

public:
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    size_t capacity() const { return capacity_; }

    double load_factor() const
    {
        return capacity_ == 0 ? 0.0 : static_cast<double>(size_) / capacity_;
    }

    size_t arena_used() const { return arena_.get_used(); }
    size_t arena_capacity() const { return arena_.get_capacity(); }

    double arena_utilization() const
    {
        return static_cast<double>(arena_.get_used()) / arena_.get_capacity();
    }
};

using HashTable64 = OpenAddressTable<uint64_t, uint64_t>;
using HashTable32 = OpenAddressTable<uint32_t, uint32_t>;
