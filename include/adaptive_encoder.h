#pragma once

#include <memory>

#include "encoder.h"

/**
 * Adaptive encoder
 * Automatically selects the best encoding method based on data characteristics
 * Analyzes data distribution and chooses the most efficient encoder
 */
class AdaptiveEncoder final : public Encoder {
public:
    std::vector<uint8_t> encode(const roaring::Roaring& roaring) override;
    roaring::Roaring decode(const std::vector<uint8_t>& data) override;
    EncodingType getType() const override { return EncodingType::ADAPTIVE; }
    const char* getName() const override { return "Adaptive"; }
};
