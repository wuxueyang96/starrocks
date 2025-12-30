#include "adaptive_encoder.h"

#include <limits>
#include <numeric>

#include "encoder_factory.h"
#include "varint_encoder.h"

Status AdaptiveEncoder::encode(const roaring::Roaring& roaring, std::vector<uint8_t>* result) {
    if (!result) {
        return Status::INVALID_INPUT;
    }

    if (roaring.isEmpty()) {
        result->clear();
        return Status::OK;
    }

    // Try all encoding types and measure actual compressed size
    const std::vector candidates = {EncodingType::VARINT, EncodingType::SIMPLE9, EncodingType::FOR_VARINT,
                                    EncodingType::PFOR_DELTA, EncodingType::NEW_PFOR_DELTA};

    std::vector<uint8_t> best_encoded;
    EncodingType best_type = EncodingType::VARINT;
    size_t best_size = std::numeric_limits<size_t>::max();

    for (const EncodingType encoding_type : candidates) {
        try {
            // Create encoder and measure output size
            const auto encoder = EncoderFactory::createEncoder(encoding_type);

            std::vector<uint8_t> encoded;
            if (encoder->encode(roaring, &encoded) == Status::OK && encoded.size() < best_size) {
                best_size = encoded.size();
                best_encoded = std::move(encoded);
                best_type = encoding_type;
            }
        } catch (...) {
        }
    }

    // Prepend encoding type byte
    result->reserve(1 + best_encoded.size());
    result->push_back(static_cast<uint8_t>(best_type));
    result->insert(result->end(), best_encoded.begin(), best_encoded.end());
    return Status::OK;
}

Status AdaptiveEncoder::encode(uint32_t value, std::vector<uint8_t>* result) {
    if (!result) {
        return Status::INVALID_INPUT;
    }
    // For single value, use VarInt as default
    result->push_back(static_cast<uint8_t>(EncodingType::VARINT));
    VarIntEncoder encoder;
    return encoder.encode(value, result);
}
