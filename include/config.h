#pragma once

#include <string>
#include <vector>
#include "compression_util.h"
#include "encoding_util.h"

struct Config {
    std::string type;
    std::string region;
    std::string endpoint;
    std::string access_key_id;
    std::string access_key_secret;
    std::string bucket_name;
    std::string object_key;
    std::vector<int32_t> column_indices;  // Column indices to read
    std::string index_output_path;    // Path to save inverted index
    std::string compression_type;     // Compression algorithm: none, lz4, snappy, zstd
    std::string encoding_type;        // Encoding type: varint, for, pfor

    static Config loadConfig(const std::string& config_path);
};
