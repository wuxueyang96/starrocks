#pragma once

#include "encoder.h"

/**
 * NewPForDelta encoder
 * Enhanced PForDelta with Simple9 for exception values
 * More efficient than standard PForDelta for small exception values
 */
class NewPForDeltaEncoder : public Encoder {
public:
    std::vector<uint8_t> encode(const std::vector<uint32_t>& values, size_t start, size_t end) override;
    std::vector<uint32_t> decode(const uint8_t* encoded, size_t size) override;
    EncodingType getType() const override { return EncodingType::NEW_PFOR_DELTA; }
    const char* getName() const override { return "NewPForDelta"; }

private:
    std::vector<uint8_t> encodeImpl(const std::vector<uint32_t>& values);
    std::vector<uint32_t> decodeImpl(const std::vector<uint8_t>& encoded);
};
