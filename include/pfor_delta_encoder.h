#pragma once

#include "encoder.h"

/**
 * PForDelta (Patched Frame of Reference with Delta) encoder
 * Uses 90th percentile to determine normal bit width
 * Stores exceptions separately with Simple9 encoding
 * Efficient for datasets with outliers
 */
class PForDeltaEncoder : public Encoder {
public:
    std::vector<uint8_t> encode(const std::vector<uint32_t>& values) override;
    std::vector<uint32_t> decode(const std::vector<uint8_t>& encoded) override;
    EncodingType getType() const override { return EncodingType::PFOR_DELTA; }
    const char* getName() const override { return "PForDelta"; }
};
