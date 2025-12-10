#pragma once

#include <string>

struct Config {
    std::string region;
    std::string endpoint;
    std::string access_key_id;
    std::string access_key_secret;
    std::string bucket_name;
    std::string object_key;
    std::vector<int> column_indices;  // Column indices to read
    std::string index_output_path;    // Path to save inverted index

    static Config loadConfig(const std::string& config_path);
};
