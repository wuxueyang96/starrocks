#pragma once

#include "encoder.h"

/**
 * PForDelta (Patched Frame of Reference with Delta) encoder
 * Uses FIXED-size blocks (128 integers per block)
 * Each block uses 90th percentile to determine bit width
 * Stores exceptions separately with Simple9 encoding
 * Efficient for datasets with outliers
 */
class PForDeltaEncoder final : public Encoder {
public:
    static constexpr size_t BLOCK_SIZE = 128; // Fixed block size
    
    std::vector<uint8_t> encode(const roaring::Roaring& roaring) override;
    roaring::Roaring decode(const std::vector<uint8_t>& data) override;
    EncodingType getType() const override { return EncodingType::PFOR_DELTA; }
    const char* getName() const override { return "PForDelta"; }

private:
    static std::vector<uint8_t> encodeBlock(const std::vector<uint32_t>& values, size_t start, size_t end);
    static size_t decodeBlock(const std::vector<uint8_t>& encoded, size_t offset, std::vector<uint32_t>& output);
};
