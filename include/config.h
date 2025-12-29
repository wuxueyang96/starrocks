#pragma once

#include <string>
#include <unordered_set>
#include <vector>

struct MemoryPoolConfig {
    size_t total_size;           // 总内存大小
    float small_block_ratio;     // 小块区域占比 (0.0 - 1.0)

    MemoryPoolConfig() : total_size(64 * 1024 * 1024), small_block_ratio(0.7f) {}
    MemoryPoolConfig(size_t size, float ratio = 0.7f)
        : total_size(size), small_block_ratio(ratio) {}
};

struct Config {
    std::string type;
    std::string region;
    std::string endpoint;
    std::string access_key_id;
    std::string access_key_secret;
    std::string bucket_name;
    std::string object_key;
    std::vector<int32_t> column_indices; // Column indices to read
    std::string index_output_path;       // Path to save inverted index
    std::string compression_type;        // Compression algorithm: none, lz4, snappy, zstd
    std::string encoding_type;           // Encoding type: varint, for, pfor, adaptive
    std::unordered_set<std::string> stop_words;
    bool enable_memory_pool;
    MemoryPoolConfig memory_pool_config;

    static Config loadConfig(const std::string& config_path);
};
