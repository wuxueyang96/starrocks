#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "arrow/api.h"
#include "bitmap_inverted_index.h"
#include "include/config.h"
#include "include/inverted_index.h"
#include "include/s3_parquet_reader.h"
#include "tokenizer.h"

AwsSdkInitializer initializer;

EncodingType get_encoding_type_from_string(const std::string& encoding) {
    if (encoding == "varint" || encoding == "VARINT") {
        return EncodingType::VARINT;
    }
    if (encoding == "simple9" || encoding == "SIMPLE9") {
        return EncodingType::SIMPLE9;
    }
    if (encoding == "for" || encoding == "FOR") {
        return EncodingType::FOR_VARINT;
    }
    if (encoding == "pfor" || encoding == "PFOR" || encoding == "pfordelta" || encoding == "PFORDELTA") {
        return EncodingType::PFOR_DELTA;
    }
    if (encoding == "newpfor" || encoding == "NEWPFOR" || encoding == "newpfordelta" || encoding == "NEWPFORDELTA") {
        return EncodingType::NEW_PFOR_DELTA;
    }
    return EncodingType::ADAPTIVE; // Default to adaptive
}

int main(int argc, char* argv[]) {
    // Timing variables
    auto start_time = std::chrono::high_resolution_clock::now();
    auto end_time = start_time;

    // Load configuration from YAML file
    std::string config_path = "config.yaml";
    if (argc > 1) {
        config_path = argv[1];
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Inverted Index Builder" << std::endl;
    std::cout << "========================================\n" << std::endl;
    std::cout << "Loading configuration from:" << config_path << std::endl;

    Config config;
    try {
        config = Config::loadConfig(config_path);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load configuration: " << e.what() << std::endl;
        return 1;
    }

    try {
        // ========== Step 1: Read parquet file from S3 ==========
        std::cout << "\n[Step 1] Reading parquet file from S3..." << std::endl;
        start_time = std::chrono::high_resolution_clock::now();

        S3ParquetReader reader(config);
        auto result = reader.readParquetFromS3();
        if (!result.ok()) {
            std::cerr << "Error reading parquet file: " << result.status().ToString() << std::endl;
            return 1;
        }
        const auto& table = result.ValueOrDie();

        end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        std::cout << "[Step 1] Completed in " << duration.count() << " ms" << std::endl;

        // ========== Step 2: Build inverted index ==========
        std::cout << "\n[Step 2] Building inverted index..." << std::endl;
        start_time = std::chrono::high_resolution_clock::now();

        InvertedIndex index;

        Tokenizer tokenizer(config);

        // Process each column
        for (int col_idx = 0; col_idx < table->num_columns(); ++col_idx) {
            auto column = table->column(col_idx);

            std::cout << "- processing column: " << col_idx << std::endl;

            // Process each chunk in the column
            for (int chunk_idx = 0; chunk_idx < column->num_chunks(); ++chunk_idx) {
                auto chunk = column->chunk(chunk_idx);

                std::cout << "  - processing chunk: " << chunk_idx << ", type " << chunk->type()->ToString()
                          << std::endl;

                // Handle string columns
                if (chunk->type()->id() == arrow::Type::BINARY) {
                    auto string_array = std::static_pointer_cast<arrow::BinaryArray>(chunk);
                    for (int64_t row = 0; row < string_array->length(); ++row) {
                        if (!string_array->IsNull(row)) {
                            std::string text = string_array->GetString(row);
                            auto doc_id = static_cast<uint32_t>(row);

                            // Tokenize and add to index
                            auto tokens = tokenizer.tokenize(text);
                            for (uint32_t pos = 0; pos < tokens.size(); ++pos) {
                                index.addTerm(tokens[pos], doc_id, pos);
                            }
                        }
                    }
                }
            }
        }

        end_time = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        std::cout << "[Step 2] Completed in " << duration.count() << " ms" << std::endl;

        // Print statistics
        index.printStatistics();

        // ========== Step 3: Compare Encoding ==========
        std::cout << "\n[Step 3] Compare different encoding for inverted index..." << std::endl;
        const std::vector candidates = {EncodingType::VARINT, EncodingType::SIMPLE9, EncodingType::FOR_VARINT,
                                        EncodingType::PFOR_DELTA, EncodingType::NEW_PFOR_DELTA};

        // Parse compression and encoding types
        CompressionType compression = CompressionUtil::stringToCompressionType(config.compression_type);

        for (const EncodingType& encoding : candidates) {
            auto start = std::chrono::high_resolution_clock::now();

            const auto encoded = index.encode(encoding, compression);

            auto end = std::chrono::high_resolution_clock::now();
            auto encoding_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            std::cout << "- encoding " << get_encoding_type_name(encoding) << " costs " << encoding_time.count()
                      << " ms, size: " << encoded.size() << " bytes." << std::endl;
        }

        end_time = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        std::cout << "[Step 3] Completed in " << duration.count() << " ms" << std::endl;

        std::cout << "\n========================================" << std::endl;
        std::cout << "  Successfully completed!" << std::endl;
        std::cout << "========================================" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}