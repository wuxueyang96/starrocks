#include "adaptive_encoder.h"

#include <algorithm>

#include "encoder_factory.h"
#include "varint_encoder.h"

EncodingType AdaptiveEncoder::selectBestEncoding(const std::vector<uint32_t>& values) {
    if (values.empty()) {
        return EncodingType::VARINT;
    }

    // For very small lists, VarInt is simplest
    if (values.size() <= 5) {
        return EncodingType::VARINT;
    }

    // Sort values to analyze distribution
    std::vector<uint32_t> sorted_values = values;
    std::sort(sorted_values.begin(), sorted_values.end());

    const uint32_t min_val = sorted_values[0];
    const uint32_t max_val = sorted_values[sorted_values.size() - 1];

    // Calculate deltas from minimum
    std::vector<uint32_t> deltas;
    deltas.reserve(sorted_values.size() - 1);
    uint32_t max_delta = 0;
    for (size_t i = 1; i < sorted_values.size(); ++i) {
        uint32_t delta = sorted_values[i] - min_val;
        deltas.push_back(delta);
        max_delta = std::max(max_delta, delta);
    }

    // Analyze delta distribution
    if (deltas.empty()) {
        return EncodingType::VARINT;
    }

    // Calculate 90th percentile
    size_t percentile_90_idx = static_cast<size_t>(deltas.size() * 0.9);
    std::vector<uint32_t> sorted_deltas = deltas;
    std::sort(sorted_deltas.begin(), sorted_deltas.end());
    uint32_t percentile_90 = sorted_deltas[percentile_90_idx];

    // Count outliers (values beyond 90th percentile)
    size_t outlier_count = 0;
    for (uint32_t delta : deltas) {
        if (delta > percentile_90) {
            outlier_count++;
        }
    }
    double outlier_ratio = static_cast<double>(outlier_count) / deltas.size();

    // Decision logic:

    // 1. If range is very small or all values are close, use FOR
    if (max_delta <= 255 && percentile_90 == max_delta) {
        return EncodingType::FOR_VARINT;
    }

    // 2. If there are significant outliers (>10%) with small exception values, use NewPForDelta
    if (outlier_ratio > 0.1 && outlier_count >= 2) {
        // Check if exceptions are small enough for Simple9
        bool exceptions_small = true;
        for (uint32_t delta : sorted_deltas) {
            if (delta > percentile_90 && delta > 268435455) { // Simple9 max value
                exceptions_small = false;
                break;
            }
        }
        return exceptions_small ? EncodingType::NEW_PFOR_DELTA : EncodingType::PFOR_DELTA;
    }

    // 3. If deltas are uniform and small, use FOR
    if (percentile_90 < 1024 && outlier_ratio < 0.05) {
        return EncodingType::FOR_VARINT;
    }

    // 4. If values are very small (< 128), VarInt is efficient
    if (max_val < 128) {
        return EncodingType::VARINT;
    }

    // 5. For moderate outliers, use NewPForDelta (better than PForDelta)
    if (outlier_ratio > 0.05) {
        return EncodingType::NEW_PFOR_DELTA;
    }

    // 6. Default to FOR for clustered data
    return EncodingType::FOR_VARINT;
}

std::vector<uint8_t> AdaptiveEncoder::encode(const std::vector<uint32_t>& values, size_t start, size_t end) {
    // Extract the range to encode
    std::vector range_values(values.begin() + start, values.begin() + end);
    selected_type_ = selectBestEncoding(range_values);
    selected_encoder_ = EncoderFactory::createEncoder(selected_type_);
    return selected_encoder_->encode(values, start, end);
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
