
/*
#ifndef DATABENTO_ORDERBOOK_LIMIT_POOL_H
#define DATABENTO_ORDERBOOK_LIMIT_POOL_H

#include <unordered_map>
#include <list>
#include <memory>
#include "Limit.h"


class limit_pool {
private:
    std::unordered_map<float,std::unique_ptr<Limit>> limits_by_price_;

public:
    Limit* acquire_limit(float price);
    void release_limit(Limit* curr);
    //~limit_pool();
};


#endif //DATABENTO_ORDERBOOK_LIMIT_POOL_H
*/