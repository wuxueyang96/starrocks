#include "pfor_delta_encoder.h"
#include "varint_encoder.h"
#include "simple9_encoder.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_map>

std::vector<uint8_t> PForDeltaEncoder::encode(const std::vector<uint32_t>& values, size_t start, size_t end) {
    // Extract the range and encode
    std::vector<uint32_t> range_values(values.begin() + start, values.begin() + end);
    return encodeImpl(range_values);
}

std::vector<uint32_t> PForDeltaEncoder::decode(const uint8_t* encoded, size_t size) {
    std::vector<uint8_t> encoded_vec(encoded, encoded + size);
    return decodeImpl(encoded_vec);
}

std::vector<uint8_t> PForDeltaEncoder::encodeImpl(const std::vector<uint32_t>& values) {
    std::vector<uint8_t> encoded;

    if (values.empty()) {
        return encoded;
    }

    // Encode the count
    VarIntEncoder::encodeValue(static_cast<uint32_t>(values.size()), encoded);

    if (values.size() == 1) {
        VarIntEncoder::encodeValue(values[0], encoded);
        return encoded;
    }

    // Find min value as base
    const uint32_t base = *std::min_element(values.begin(), values.end());
    VarIntEncoder::encodeValue(base, encoded);

    // Calculate deltas from base (preserve original order)
    std::vector<uint32_t> deltas;
    deltas.reserve(values.size());

    for (size_t i = 0; i < values.size(); ++i) {
        deltas.push_back(values[i] - base);
    }

    if (deltas.empty()) {
        encoded.push_back(0); // bits_per_value = 0
        return encoded;
    }

    // Analyze deltas to find optimal bit width and exceptions
    // Use 90th percentile to determine normal bit width
    std::vector<uint32_t> sorted_deltas = deltas;
    std::sort(sorted_deltas.begin(), sorted_deltas.end());

    const size_t percentile_90_idx = std::min(static_cast<size_t>(sorted_deltas.size() * 0.9), sorted_deltas.size() - 1);
    const uint32_t threshold = sorted_deltas[percentile_90_idx];
    const uint8_t bits_per_value = threshold == 0 ? 0 : (32 - __builtin_clz(threshold));

    // Find exceptions (values that don't fit in bits_per_value)
    const uint32_t max_value = bits_per_value == 0 ? 0 : ((1U << bits_per_value) - 1);
    std::vector<uint32_t> exception_indices;
    std::vector<uint32_t> exception_values;

    for (size_t i = 0; i < deltas.size(); ++i) {
        if (deltas[i] > max_value) {
            exception_indices.push_back(static_cast<uint32_t>(i));
            exception_values.push_back(deltas[i]);
        }
    }

    // Encode bits per value
    encoded.push_back(bits_per_value);

    // Encode number of exceptions
    VarIntEncoder::encodeValue(static_cast<uint32_t>(exception_indices.size()), encoded);

    // Encode exception indices using VarInt (indices are usually small)
    for (uint32_t idx : exception_indices) {
        VarIntEncoder::encodeValue(idx, encoded);
    }

    // Encode exception values using Simple9 (more efficient than VarInt)
    if (!exception_values.empty()) {
        size_t exc_idx = 0;
        while (exc_idx < exception_values.size()) {
            const size_t encoded_count = Simple9Encoder::encodeBatch(exception_values, exc_idx, encoded);
            exc_idx += encoded_count;
        }
    }

    if (bits_per_value == 0) {
        // All values fit in 0 bits (all same)
        return encoded;
    }

    // Bit-pack the regular values (clamped to max_value for exceptions)
    uint64_t buffer = 0;
    size_t bit_pos = 0;

    for (uint32_t delta : deltas) {
        const uint32_t clamped_value = std::min(delta, max_value);
        buffer |= (static_cast<uint64_t>(clamped_value) << bit_pos);
        bit_pos += bits_per_value;

        while (bit_pos >= 8) {
            encoded.push_back(static_cast<uint8_t>(buffer & 0xFF));
            buffer >>= 8;
            bit_pos -= 8;
        }
    }

    // Flush remaining bits
    if (bit_pos > 0) {
        encoded.push_back(static_cast<uint8_t>(buffer & 0xFF));
    }

    return encoded;
}

std::vector<uint32_t> PForDeltaEncoder::decodeImpl(const std::vector<uint8_t>& encoded) {
    std::vector<uint32_t> values;

    if (encoded.empty()) {
        return values;
    }

    size_t offset = 0;

    // Decode count
    const uint32_t count = VarIntEncoder::decodeValue(encoded, offset);
    if (count == 0) {
        return values;
    }

    values.reserve(count);

    // Decode base value
    const uint32_t base = VarIntEncoder::decodeValue(encoded, offset);

    if (count == 1) {
        values.push_back(base);
        return values;
    }

    // Decode bits per value
    if (offset >= encoded.size()) {
        throw std::runtime_error("Invalid PForDelta encoding: missing bits_per_value");
    }
    const uint8_t bits_per_value = encoded[offset++];

    // Decode number of exceptions
    const uint32_t num_exceptions = VarIntEncoder::decodeValue(encoded, offset);

    // Decode exception indices
    std::vector<uint32_t> exception_indices;
    exception_indices.reserve(num_exceptions);
    for (uint32_t i = 0; i < num_exceptions; ++i) {
        exception_indices.push_back(VarIntEncoder::decodeValue(encoded, offset));
    }

    // Decode exception values using Simple9
    std::vector<uint32_t> exception_values;
    exception_values.reserve(num_exceptions);
    while (exception_values.size() < num_exceptions && offset < encoded.size()) {
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
                throw std::runtime_error("Invalid Simple9 selector in PForDelta");
            }

            const auto& mode = Simple9Encoder::MODES[selector];
            const uint32_t mask = (1U << mode.bits) - 1;

            // Extract integers
            for (uint8_t i = 0; i < mode.count && exception_values.size() < num_exceptions; ++i) {
                const uint32_t value = (word >> (i * mode.bits)) & mask;
                exception_values.push_back(value);
            }
        } else {
            // Fallback: read VarInt
            exception_values.push_back(VarIntEncoder::decodeValue(encoded, offset));
        }
    }

    // Build exception map
    std::unordered_map<uint32_t, uint32_t> exceptions;
    for (size_t i = 0; i < exception_indices.size(); ++i) {
        exceptions[exception_indices[i]] = exception_values[i];
    }

    if (bits_per_value == 0) {
        // All values are the same
        for (uint32_t i = 0; i < count; ++i) {
            const auto it = exceptions.find(i);
            const uint32_t delta = (it != exceptions.end()) ? it->second : 0;
            values.push_back(base + delta);
        }
        return values;
    }

    // Unpack regular values
    std::vector<uint32_t> deltas;
    deltas.reserve(count);

    uint64_t buffer = 0;
    size_t buffered_bits = 0;

    for (size_t i = 0; i < count; ++i) {
        // Ensure we have enough bits in buffer
        while (buffered_bits < bits_per_value && offset < encoded.size()) {
            buffer |= (static_cast<uint64_t>(encoded[offset++]) << buffered_bits);
            buffered_bits += 8;
        }

        // Extract delta
        const uint64_t mask = (1ULL << bits_per_value) - 1;
        uint32_t delta = static_cast<uint32_t>(buffer & mask);

        // Replace with exception value if exists
        const auto it = exceptions.find(static_cast<uint32_t>(i));
        if (it != exceptions.end()) {
            delta = it->second;
        }

        deltas.push_back(delta);
        values.push_back(base + delta);

        buffer >>= bits_per_value;
        buffered_bits -= bits_per_value;
    }

    return values;
}
