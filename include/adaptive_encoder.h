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
    std::vector<uint8_t> encode(const std::vector<uint32_t>& values, size_t start, size_t end) override;
    std::vector<uint32_t> decode(const uint8_t* encoded, size_t size) override;
    EncodingType getType() const override { return EncodingType::ADAPTIVE; }
    const char* getName() const override { return "Adaptive"; }
};
