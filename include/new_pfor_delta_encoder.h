#pragma once

#include "encoder.h"

/**
 * NewPForDelta encoder
 * Uses VARIABLE-size blocks with adaptive block sizing
 * Dynamically determines optimal block size based on data characteristics
 * More flexible than standard PForDelta for different data distributions
 * Exception values encoded with Simple9
 */
class NewPForDeltaEncoder final : public Encoder {
public:
    static constexpr size_t MIN_BLOCK_SIZE = 32;   // Minimum block size
    static constexpr size_t MAX_BLOCK_SIZE = 256;  // Maximum block size
    static constexpr size_t DEFAULT_BLOCK_SIZE = 128; // Default/target block size
    
    std::vector<uint8_t> encode(const roaring::Roaring& roaring) override;
    roaring::Roaring decode(const std::vector<uint8_t>& data) override;
    EncodingType getType() const override { return EncodingType::NEW_PFOR_DELTA; }
    const char* getName() const override { return "NewPForDelta"; }

private:
    static size_t determineBlockSize(const std::vector<uint32_t>& values, size_t start, size_t end);
    static std::vector<uint8_t> encodeBlock(const std::vector<uint32_t>& values, size_t start, size_t end);
    static size_t decodeBlock(const std::vector<uint8_t>& encoded, size_t offset, size_t end_offset, std::vector<uint32_t>& output);
};
