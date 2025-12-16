#include "adaptive_encoder.h"

#include "encoder_factory.h"
#include "varint_encoder.h"

std::vector<uint8_t> AdaptiveEncoder::encode(const std::vector<uint32_t>& values, size_t start, size_t end) {
    if (values.empty() || start >= end) {
        return {};
    }

    // Try all encoding types and measure actual compressed size
    const std::vector candidates = {
        EncodingType::VARINT,
        EncodingType::SIMPLE9,
        EncodingType::FOR_VARINT,
        EncodingType::PFOR_DELTA,
        EncodingType::NEW_PFOR_DELTA
    };

    std::vector<uint8_t> result;
    size_t best_size = std::numeric_limits<size_t>::max();

    for (const EncodingType encoding_type : candidates) {
        try {
            // Create encoder and measure output size
            const auto encoder = EncoderFactory::createEncoder(encoding_type);

            if (std::vector<uint8_t> encoded = encoder->encode(values, start, end); encoded.size() < best_size) {
                best_size = encoded.size();
                result = std::move(encoded);
            }
        } catch (...) {
        }
    }
    return result;
}

std::vector<uint32_t> AdaptiveEncoder::decode(const uint8_t* encoded, size_t size) {
    // Note: Adaptive decoder needs to know which encoding was used
    // In practice, this information should be stored with the encoded data
    // For now, we'll use the last selected encoder
    if (!selected_encoder_) {
        // Default to VarInt if no encoder was selected
        selected_encoder_ = std::make_shared<VarIntEncoder>();
    }
    return selected_encoder_->decode(encoded, size);
}
