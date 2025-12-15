#pragma once

#include <memory>

#include "encoder.h"

/**
 * Adaptive encoder
 * Automatically selects the best encoding method based on data characteristics
 * Analyzes data distribution and chooses the most efficient encoder
 */
class AdaptiveEncoder : public Encoder {
public:
    std::vector<uint8_t> encode(const std::vector<uint32_t>& values) override;
    std::vector<uint32_t> decode(const std::vector<uint8_t>& encoded) override;
    EncodingType getType() const override { return EncodingType::ADAPTIVE; }
    const char* getName() const override { return "Adaptive"; }

    // Get the encoding type that was selected during last encode
    EncodingType getSelectedType() const { return selected_type_; }

    // Get the encoder that was selected during last encode
    std::shared_ptr<Encoder> getSelectedEncoder() const { return selected_encoder_; }

private:
    EncodingType selected_type_ = EncodingType::VARINT;
    std::shared_ptr<Encoder> selected_encoder_;

    // Analyze data and choose best encoding type
    EncodingType selectBestEncoding(const std::vector<uint32_t>& values);

    // Create encoder instance based on type
    std::shared_ptr<Encoder> createEncoder(EncodingType type);
};
