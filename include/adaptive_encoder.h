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
    Status encode(const roaring::Roaring& roaring, std::vector<uint8_t>* result) override;
    Status encode(uint32_t value, std::vector<uint8_t>* result) override;
    EncodingType getType() const override { return EncodingType::ADAPTIVE; }
    const char* getName() const override { return "Adaptive"; }
};
