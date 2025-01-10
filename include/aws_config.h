#pragma once
#include <string>

struct AWSConfig {
    static constexpr char BUCKET_NAME[] = "hft-backtest-data";
    static constexpr char REGION[] = "us-east-1";
    static constexpr char CACHE_DIR[] = "./cache";
};

