#include "simple9_encoder.h"
#include "varint_encoder.h"
#include <algorithm>
#include <stdexcept>

// Define static constexpr member
constexpr Simple9Encoder::Simple9Mode Simple9Encoder::MODES[9];

size_t Simple9Encoder::encodeBatch(const std::vector<uint32_t>& values, size_t start_idx,
                                    std::vector<uint8_t>& output) {
    const size_t remaining = values.size() - start_idx;
    if (remaining == 0) {
        return 0;
    }

    // Find the best mode that can encode the most integers
    int best_mode = -1;
    size_t best_count = 0;

    for (int mode = 0; mode < 9; ++mode) {
        const auto& m = MODES[mode];
        const size_t can_encode = std::min(remaining, static_cast<size_t>(m.count));

        // Check if all values in this range fit in this mode
        bool fits = true;
        for (size_t i = 0; i < can_encode; ++i) {
            if (values[start_idx + i] > m.max_value) {
                fits = false;
                break;
            }
        }

        if (fits && can_encode > best_count) {
            best_mode = mode;
            best_count = can_encode;
        }
    }

    // If no mode fits, use VarInt fallback for this value
    if (best_mode == -1) {
        VarIntEncoder::encodeValue(values[start_idx], output);
        return 1;
    }

    // Pack integers using the best mode
    const auto& mode = MODES[best_mode];
    uint32_t word = static_cast<uint32_t>(best_mode) << 28; // 4-bit selector

    for (size_t i = 0; i < best_count; ++i) {
        word |= (values[start_idx + i] << (i * mode.bits));
    }

    // Write 32-bit word (little-endian)
    output.push_back(static_cast<uint8_t>(word & 0xFF));
    output.push_back(static_cast<uint8_t>((word >> 8) & 0xFF));
    output.push_back(static_cast<uint8_t>((word >> 16) & 0xFF));
    output.push_back(static_cast<uint8_t>((word >> 24) & 0xFF));

    return best_count;
}

std::vector<uint8_t> Simple9Encoder::encode(const std::vector<uint32_t>& values, size_t start, size_t end) {
    // Extract the range and encode
    std::vector<uint32_t> range_values(values.begin() + start, values.begin() + end);
    return encodeImpl(range_values);
}

std::vector<uint32_t> Simple9Encoder::decode(const uint8_t* encoded, size_t size) {
    std::vector<uint8_t> encoded_vec(encoded, encoded + size);
    return decodeImpl(encoded_vec);
}

std::vector<uint8_t> Simple9Encoder::encodeImpl(const std::vector<uint32_t>& values) {
    std::vector<uint8_t> encoded;

    if (values.empty()) {
        return encoded;
    }

    // Encode values in batches
    size_t idx = 0;
    while (idx < values.size()) {
        const size_t encoded_count = encodeBatch(values, idx, encoded);
        idx += encoded_count;
    }

    return encoded;
}

std::vector<uint32_t> Simple9Encoder::decodeImpl(const std::vector<uint8_t>& encoded) {
    std::vector<uint32_t> values;

    if (encoded.empty()) {
        return values;
    }

    size_t offset = 0;

    // Decode values until we reach the end
    while (offset < encoded.size()) {
        // Check if we have enough bytes for a 32-bit word
        if (offset + 4 <= encoded.size()) {
            // Read 32-bit word (little-endian)
            const uint32_t word = static_cast<uint32_t>(encoded[offset]) |
                                  (static_cast<uint32_t>(encoded[offset + 1]) << 8) |
                                  (static_cast<uint32_t>(encoded[offset + 2]) << 16) |
                                  (static_cast<uint32_t>(encoded[offset + 3]) << 24);
            offset += 4;

            // Extract selector (top 4 bits)
            const uint8_t selector = static_cast<uint8_t>(word >> 28);

            if (selector >= 9) {
                throw std::runtime_error("Invalid Simple9 selector");
            }

            const auto& mode = MODES[selector];
            const uint32_t mask = (1U << mode.bits) - 1;

            // Extract integers
            for (uint8_t i = 0; i < mode.count; ++i) {
                const uint32_t value = (word >> (i * mode.bits)) & mask;
                values.push_back(value);
            }
        } else {
            // Fallback: read VarInt
            values.push_back(VarIntEncoder::decodeValue(encoded, offset));
        }
    }

    return values;
}
