#include "adaptive_encoder.h"

#include <limits>
#include <numeric>

#include "encoder_factory.h"

std::vector<uint8_t> AdaptiveEncoder::encode(const roaring::Roaring& roaring) {
    if (roaring.isEmpty()) {
        return {};
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

            if (std::vector<uint8_t> encoded = encoder->encode(roaring); encoded.size() < best_size) {
                best_size = encoded.size();
                best_encoded = std::move(encoded);
                best_type = encoding_type;
            }
        } catch (...) {
        }
    }
    
    // Prepend encoding type byte
    std::vector<uint8_t> result;
    result.reserve(1 + best_encoded.size());
    result.push_back(static_cast<uint8_t>(best_type));
    result.insert(result.end(), best_encoded.begin(), best_encoded.end());
    return result;
}

roaring::Roaring AdaptiveEncoder::decode(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return roaring::Roaring();
    }
    
    // Read encoding type from first byte
    const EncodingType encoding_type = static_cast<EncodingType>(data[0]);
    
    // Create appropriate decoder and decode remaining data
    const auto encoder = EncoderFactory::createEncoder(encoding_type);
    std::vector<uint8_t> encoded_data(data.begin() + 1, data.end());
    return encoder->decode(encoded_data);
}
