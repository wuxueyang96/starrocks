#pragma once

#include "encoder.h"

/**
 * Frame of Reference (FOR) encoder
 * Uses a base value and encodes differences with bit-packing
 * Efficient for values with uniform distribution and small range
 */
class FrameOfReferenceEncoder final : public Encoder {
public:
    std::vector<uint8_t> encode(const std::vector<uint32_t>& values, size_t start, size_t end) override;
    std::vector<uint32_t> decode(const uint8_t* encoded, size_t size) override;
    EncodingType getType() const override { return EncodingType::FOR_VARINT; }
    const char* getName() const override { return "FrameOfReference"; }
};
