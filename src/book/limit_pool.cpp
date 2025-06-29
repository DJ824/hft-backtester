#include "limit.cpp"
#include <vector>
#include <memory>


class LimitPool
{
private:
    std::vector<std::unique_ptr<Limit>> pool_;
    std::vector<Limit*> available_;

public:
    explicit LimitPool(std::size_t initial_size = 1024)
    {
        pool_.reserve(initial_size);
        available_.reserve(initial_size);
        for (std::size_t i = 0; i < initial_size; ++i)
        {
            pool_.push_back(std::make_unique<Limit>(0));
            available_.push_back(pool_.back().get());
        }
    }

    __attribute__((always_inline))
    Limit* acquire(int32_t price, bool side)
    {
        Limit* l;
        if (available_.empty())
        {
            pool_.push_back(std::make_unique<Limit>(price));
            l = pool_.back().get();
        }
        else
        {
            l = available_.back();
            available_.pop_back();
            l->reset();
            l->price_ = price;
        }
        l->side_ = side;
        return l;
    }

    __attribute__((always_inline))
    void release(Limit* l) { available_.push_back(l); }

    __attribute__((always_inline))
    void reset(std::size_t reserve_hint = 1024)
    {
        pool_.clear();
        available_.clear();
        pool_.reserve(reserve_hint);
        available_.reserve(reserve_hint);
    }
};
