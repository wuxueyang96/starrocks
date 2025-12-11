#include "config.h"

#include <iostream>

#include "yaml-cpp/yaml.h"

Config Config::loadConfig(const std::string& config_path) {
    Config config;
    try {
        YAML::Node yaml = YAML::LoadFile(config_path);

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

        config.index_output_path = "/tmp/inverted_index.bin";
        if (yaml["inverted_index"] && yaml["index_output_path"]["index_output_path"]) {
            config.index_output_path = yaml["inverted_index"]["index_output_path"].as<std::string>();
        }

    } catch (const YAML::Exception& e) {
        std::cerr << "Error loading config file: " << e.what() << std::endl;
        throw;
    }
    return config;
}