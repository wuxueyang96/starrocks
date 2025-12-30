#include "config.h"

#include <iostream>

#include "yaml-cpp/yaml.h"

Config Config::loadConfig(const std::string& config_path) {
    Config config;
    try {
        YAML::Node yaml = YAML::LoadFile(config_path);

        if (yaml["type"]) {
            config.type = yaml["type"].as<std::string>();
        }

        if (yaml["aws"]) {
            auto aws = yaml["aws"];
            config.region = aws["region"].as<std::string>();
            config.endpoint = aws["endpoint"].as<std::string>();
            config.access_key_id = aws["access_key_id"].as<std::string>();
            config.access_key_secret = aws["access_key_secret"].as<std::string>();
        }

        if (yaml["s3"]) {
            auto s3 = yaml["s3"];
            config.bucket_name = s3["bucket_name"].as<std::string>();
            config.object_key = s3["object_key"].as<std::string>();
        }

        if (yaml["parquet"]) {
            if (auto parquet = yaml["parquet"]; parquet["column_indices"]) {
                config.column_indices = parquet["column_indices"].as<std::vector<int>>();
            }
        }

        if (yaml["tokenizer"] && yaml["tokenizer"]["stop_words"]) {
            auto stop_words = yaml["tokenizer"]["stop_words"].as<std::vector<std::string>>();
            config.stop_words.insert(stop_words.begin(), stop_words.end());
        }

        if (yaml["memory"]) {
            auto memory = yaml["memory"];
            config.enable_memory_pool = yaml["memory"]["enable"].as<bool>();
            config.memory_pool_config.total_size = yaml["memory"]["total_size"].as<size_t>();
            config.memory_pool_config.small_block_ratio = yaml["memory"]["small_block_ratio"].as<float>();
            config.memory_pool_config.enable_logging = yaml["memory"]["enable_logging"].as<bool>();
        }

        config.index_output_path = "/tmp/inverted_index.bin";
        if (yaml["inverted_index"] && yaml["inverted_index"]["index_output_path"]) {
            config.index_output_path = yaml["inverted_index"]["index_output_path"].as<std::string>();
        }

        // Load compression settings
        config.compression_type = "none";
        if (yaml["parquet"] && yaml["parquet"]["compression"]) {
            config.compression_type = yaml["parquet"]["compression"].as<std::string>();
        }

        // Load encoding settings
        config.encoding_type = "adaptive";
        if (yaml["parquet"] && yaml["parquet"]["encoding"]) {
            config.encoding_type = yaml["parquet"]["encoding"].as<std::string>();
        }

    } catch (const YAML::Exception& e) {
        std::cerr << "Error loading config file: " << e.what() << std::endl;
        throw;
    }
    return config;
}