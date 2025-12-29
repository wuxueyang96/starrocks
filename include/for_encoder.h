#pragma once

#include "encoder.h"

/**
 * Frame of Reference (FOR) encoder
 * Uses a base value and encodes differences with bit-packing
 * Efficient for values with uniform distribution and small range
 */
class FrameOfReferenceEncoder final : public Encoder {
public:
    Status encode(const roaring::Roaring& roaring, std::vector<uint8_t>* result) override;
    Status encode(uint32_t value, std::vector<uint8_t>* result) override;
    EncodingType getType() const override { return EncodingType::FOR_VARINT; }
    const char* getName() const override { return "FrameOfReference"; }
};
