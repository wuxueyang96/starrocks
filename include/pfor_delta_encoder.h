#pragma once

#include "encoder.h"

/**
 * PForDelta (Patched Frame of Reference with Delta) encoder
 * Uses 90th percentile to determine normal bit width
 * Stores exceptions separately with Simple9 encoding
 * Efficient for datasets with outliers
 */
class PForDeltaEncoder final : public Encoder {
public:
    std::vector<uint8_t> encode(const std::vector<uint32_t>& values, size_t start, size_t end) override;
    std::vector<uint32_t> decode(const uint8_t* encoded, size_t size) override;
    EncodingType getType() const override { return EncodingType::PFOR_DELTA; }
    const char* getName() const override { return "PForDelta"; }

private:
    std::vector<uint8_t> encodeImpl(const std::vector<uint32_t>& values);
    std::vector<uint32_t> decodeImpl(const std::vector<uint8_t>& encoded);
};
