#pragma once

#include <lz4.h>
#include <snappy.h>
#include <zstd.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

enum class CompressionType { NONE = 0, LZ4 = 1, SNAPPY = 2, ZSTD = 3 };

class CompressionUtil {
public:
    // Compress data using specified algorithm
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& data, CompressionType type) {
        if (data.empty() || type == CompressionType::NONE) {
            return data;
        }

        switch (type) {
        case CompressionType::LZ4:
            return compressLZ4(data);
        case CompressionType::SNAPPY:
            return compressSnappy(data);
        case CompressionType::ZSTD:
            return compressZSTD(data);
        default:
            return data;
        }
    }

    // Decompress data using specified algorithm
    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& data, CompressionType type,
                                           size_t original_size) {
        if (data.empty() || type == CompressionType::NONE) {
            return data;
        }

        switch (type) {
        case CompressionType::LZ4:
            return decompressLZ4(data, original_size);
        case CompressionType::SNAPPY:
            return decompressSnappy(data);
        case CompressionType::ZSTD:
            return decompressZSTD(data, original_size);
        default:
            return data;
        }
    }

    static std::string compressionTypeToString(CompressionType type) {
        switch (type) {
        case CompressionType::NONE:
            return "NONE";
        case CompressionType::LZ4:
            return "LZ4";
        case CompressionType::SNAPPY:
            return "SNAPPY";
        case CompressionType::ZSTD:
            return "ZSTD";
        default:
            return "UNKNOWN";
        }
    }

    static CompressionType stringToCompressionType(const std::string& str) {
        if (str == "LZ4" || str == "lz4") return CompressionType::LZ4;
        if (str == "SNAPPY" || str == "snappy") return CompressionType::SNAPPY;
        if (str == "ZSTD" || str == "zstd") return CompressionType::ZSTD;
        return CompressionType::NONE;
    }

private:
    static std::vector<uint8_t> compressLZ4(const std::vector<uint8_t>& data) {
        const int max_compressed_size = LZ4_compressBound(static_cast<int>(data.size()));
        std::vector<uint8_t> compressed(max_compressed_size);

        const int compressed_size = LZ4_compress_default(reinterpret_cast<const char*>(data.data()),
                                                         reinterpret_cast<char*>(compressed.data()),
                                                         static_cast<int>(data.size()), max_compressed_size);

        if (compressed_size <= 0) {
            throw std::runtime_error("LZ4 compression failed");
        }

        compressed.resize(compressed_size);
        return compressed;
    }

    static std::vector<uint8_t> decompressLZ4(const std::vector<uint8_t>& data, size_t original_size) {
        std::vector<uint8_t> decompressed(original_size);

        const int result = LZ4_decompress_safe(reinterpret_cast<const char*>(data.data()),
                                               reinterpret_cast<char*>(decompressed.data()),
                                               static_cast<int>(data.size()), static_cast<int>(original_size));

        if (result < 0) {
            throw std::runtime_error("LZ4 decompression failed");
        }

        return decompressed;
    }

    static std::vector<uint8_t> compressSnappy(const std::vector<uint8_t>& data) {
        size_t compressed_size = snappy::MaxCompressedLength(data.size());
        std::vector<uint8_t> compressed(compressed_size);

        snappy::RawCompress(reinterpret_cast<const char*>(data.data()), data.size(),
                            reinterpret_cast<char*>(compressed.data()), &compressed_size);

        compressed.resize(compressed_size);
        return compressed;
    }

    static std::vector<uint8_t> decompressSnappy(const std::vector<uint8_t>& data) {
        size_t uncompressed_size;
        if (!snappy::GetUncompressedLength(reinterpret_cast<const char*>(data.data()), data.size(),
                                           &uncompressed_size)) {
            throw std::runtime_error("Snappy: failed to get uncompressed length");
        }

        std::vector<uint8_t> decompressed(uncompressed_size);
        if (!snappy::RawUncompress(reinterpret_cast<const char*>(data.data()), data.size(),
                                   reinterpret_cast<char*>(decompressed.data()))) {
            throw std::runtime_error("Snappy decompression failed");
        }

        return decompressed;
    }

    static std::vector<uint8_t> compressZSTD(const std::vector<uint8_t>& data) {
        const size_t max_compressed_size = ZSTD_compressBound(data.size());
        std::vector<uint8_t> compressed(max_compressed_size);

        const size_t compressed_size =
                ZSTD_compress(compressed.data(), max_compressed_size, data.data(), data.size(), ZSTD_CLEVEL_DEFAULT);

        if (ZSTD_isError(compressed_size)) {
            throw std::runtime_error(std::string("ZSTD compression failed: ") + ZSTD_getErrorName(compressed_size));
        }

        compressed.resize(compressed_size);
        return compressed;
    }

    static std::vector<uint8_t> decompressZSTD(const std::vector<uint8_t>& data, size_t original_size) {
        std::vector<uint8_t> decompressed(original_size);

        const size_t result = ZSTD_decompress(decompressed.data(), original_size, data.data(), data.size());

        if (ZSTD_isError(result)) {
            throw std::runtime_error(std::string("ZSTD decompression failed: ") + ZSTD_getErrorName(result));
        }

        return decompressed;
    }
};
