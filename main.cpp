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

int build_inverted_index(const std::shared_ptr<arrow::Table>& table, const Config& config) {
    // ========== Step 2: Build inverted index ==========
    std::cout << "\n[Step 2] Building inverted index..." << std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();

    InvertedIndex index;

    Tokenizer tokenizer(config);

    // Process each column
    for (int col_idx = 0; col_idx < table->num_columns(); ++col_idx) {
        auto column = table->column(col_idx);

        std::cout << "- processing column: " << col_idx << std::endl;

        // Process each chunk in the column
        for (int chunk_idx = 0; chunk_idx < column->num_chunks(); ++chunk_idx) {
            auto chunk = column->chunk(chunk_idx);

            std::cout << "  - processing chunk: " << chunk_idx << ", type " << chunk->type()->ToString() << std::endl;

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

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "[Step 2] Completed in " << duration.count() << " ms" << std::endl;

    // Print statistics
    index.printStatistics();

    // ========== Step 3: Save index to disk ==========
    std::cout << "\n[Step 3] Saving inverted index to disk..." << std::endl;
    std::cout << "Output path: " << config.index_output_path << std::endl;
    std::cout << "Compression: " << config.compression_type << std::endl;
    std::cout << "Encoding: " << config.encoding_type << std::endl;
    std::cout << "Block encoding: enabled (block size: 128)" << std::endl;
    start_time = std::chrono::high_resolution_clock::now();

    // Parse compression and encoding types
    CompressionType compression = CompressionUtil::stringToCompressionType(config.compression_type);
    EncodingType encoding = EncodingType::ADAPTIVE; // Default to adaptive

    if (config.encoding_type == "varint" || config.encoding_type == "VARINT") {
        encoding = EncodingType::VARINT;
    } else if (config.encoding_type == "for" || config.encoding_type == "FOR") {
        encoding = EncodingType::FOR_VARINT;
    } else if (config.encoding_type == "pfor" || config.encoding_type == "PFOR" ||
               config.encoding_type == "pfordelta" || config.encoding_type == "PFORDELTA") {
        encoding = EncodingType::PFOR_DELTA;
    } else if (config.encoding_type == "adaptive" || config.encoding_type == "ADAPTIVE" ||
               config.encoding_type == "auto" || config.encoding_type == "AUTO") {
        encoding = EncodingType::ADAPTIVE;
    }

    if (!index.saveToDisk(config.index_output_path, compression, encoding)) {
        std::cerr << "Error: Failed to save index to disk" << std::endl;
        return 1;
    }

    end_time = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "[Step 3] Completed in " << duration.count() << " ms" << std::endl;
    return 0;
}

// Specialization for BitmapInvertedIndex (doesn't support compression/encoding yet)
int build_bitmap_inverted_index(const std::shared_ptr<arrow::Table>& table, const Config& config) {
    // ========== Step 2: Build inverted index ==========
    std::cout << "\n[Step 2] Building inverted index..." << std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();

    BitmapInvertedIndex index;

    Tokenizer tokenizer(config);

    // Process each column
    for (int col_idx = 0; col_idx < table->num_columns(); ++col_idx) {
        auto column = table->column(col_idx);

        std::cout << "- processing column: " << col_idx << std::endl;

        // Process each chunk in the column
        for (int chunk_idx = 0; chunk_idx < column->num_chunks(); ++chunk_idx) {
            auto chunk = column->chunk(chunk_idx);

            std::cout << "  - processing chunk: " << chunk_idx << ", type " << chunk->type()->ToString() << std::endl;

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

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "[Step 2] Completed in " << duration.count() << " ms" << std::endl;

    // Print statistics
    index.printStatistics();

    // ========== Step 3: Save index to disk ==========
    std::cout << "\n[Step 3] Saving inverted index to disk..." << std::endl;
    std::cout << "Output path: " << config.index_output_path << std::endl;
    std::cout << "Note: BitmapInvertedIndex uses its own format (compression/encoding not yet supported)" << std::endl;
    start_time = std::chrono::high_resolution_clock::now();

    if (!index.saveToDisk(config.index_output_path)) {
        std::cerr << "Error: Failed to save index to disk" << std::endl;
        return 1;
    }

    end_time = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "[Step 3] Completed in " << duration.count() << " ms" << std::endl;
    return 0;
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

        int ret = 0;
        if (config.type == "bitmap") {
            ret = build_bitmap_inverted_index(table, config);
        } else {
            ret = build_inverted_index(table, config);
        }

        if (ret != 0) {
            return ret;
        }

        std::cout << "\n========================================" << std::endl;
        std::cout << "  Successfully completed!" << std::endl;
        std::cout << "========================================" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}