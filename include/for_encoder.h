#pragma once

#include "encoder.h"

/**
 * Frame of Reference (FOR) encoder
 * Uses a base value and encodes differences with bit-packing
 * Efficient for values with uniform distribution and small range
 */
class FrameOfReferenceEncoder final : public Encoder {
public:
    std::vector<uint8_t> encode(const roaring::Roaring& roaring) override;
    roaring::Roaring decode(const std::vector<uint8_t>& data) override;
    EncodingType getType() const override { return EncodingType::FOR_VARINT; }
    const char* getName() const override { return "FrameOfReference"; }
};
