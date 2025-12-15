#pragma once

#include "encoder.h"

/**
 * NewPForDelta encoder
 * Enhanced PForDelta with Simple9 for exception values
 * More efficient than standard PForDelta for small exception values
 */
class NewPForDeltaEncoder : public Encoder {
public:
    std::vector<uint8_t> encode(const std::vector<uint32_t>& values) override;
    std::vector<uint32_t> decode(const std::vector<uint8_t>& encoded) override;
    EncodingType getType() const override { return EncodingType::NEW_PFOR_DELTA; }
    const char* getName() const override { return "NewPForDelta"; }
};
