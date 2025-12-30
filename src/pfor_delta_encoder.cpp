#include "pfor_delta_encoder.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

#include "simple9_encoder.h"
#include "varint_encoder.h"

Status PForDeltaEncoder::encode(const roaring::Roaring& roaring, std::vector<uint8_t>* result) {
    if (!result) {
        return Status::INVALID_INPUT;
    }

    if (roaring.isEmpty()) {
        result->clear();
        return Status::OK;
    }

    std::vector<uint32_t> values(roaring.cardinality());
    roaring.toUint32Array(values.data());
    const size_t start = 0;
    const size_t end = values.size();

    const size_t total_count = end - start;

    // Encode total count
    VarIntEncoder::encodeValue(static_cast<uint32_t>(total_count), *result);

    // Process in fixed-size blocks
    size_t pos = start;
    while (pos < end) {
        const size_t block_end = std::min(pos + BLOCK_SIZE, end);
        const std::vector<uint8_t> block_data = encodeBlock(values, pos, block_end);

        // Just append block data directly - no need to store size since we know BLOCK_SIZE
        result->insert(result->end(), block_data.begin(), block_data.end());

        pos = block_end;
    }

    return Status::OK;
}

Status PForDeltaEncoder::encode(uint32_t value, std::vector<uint8_t>* result) {
    if (!result) {
        return Status::INVALID_INPUT;
    }
    VarIntEncoder::encodeValue(1, *result); // count = 1
    VarIntEncoder::encodeValue(value, *result);
    return Status::OK;
}

std::vector<uint8_t> PForDeltaEncoder::encodeBlock(const std::vector<uint32_t>& values, size_t start, size_t end) {
    std::vector<uint8_t> encoded;

    if (start >= end) {
        return encoded;
    }

    const size_t block_size = end - start;

    if (block_size == 1) {
        VarIntEncoder::encodeValue(values[start], encoded);
        return encoded;
    }

    // Find min value as base for this block
    uint32_t base = values[start];
    for (size_t i = start + 1; i < end; ++i) {
        base = std::min(base, values[i]);
    }
    VarIntEncoder::encodeValue(base, encoded);

    // Calculate deltas from base
    std::vector<uint32_t> deltas;
    deltas.reserve(block_size);
    for (size_t i = start; i < end; ++i) {
        deltas.push_back(values[i] - base);
    }

    // Find 90th percentile for bit width
    std::vector<uint32_t> sorted_deltas = deltas;
    std::sort(sorted_deltas.begin(), sorted_deltas.end());

    const size_t percentile_90_idx =
            std::min(static_cast<size_t>(sorted_deltas.size() * 0.9), sorted_deltas.size() - 1);
    const uint32_t threshold = sorted_deltas[percentile_90_idx];
    const uint8_t bits_per_value = threshold == 0 ? 0 : (32 - __builtin_clz(threshold));

    // Find exceptions
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

    // Encode exception indices using VarInt
    for (uint32_t idx : exception_indices) {
        VarIntEncoder::encodeValue(idx, encoded);
    }

    // Encode exception values using Simple9
    if (!exception_values.empty()) {
        size_t exc_idx = 0;
        while (exc_idx < exception_values.size()) {
            const size_t encoded_count = Simple9Encoder::encodeBatch(exception_values, exc_idx, encoded);
            exc_idx += encoded_count;
        }
    }

    if (bits_per_value == 0) {
        return encoded;
    }

    // Bit-pack the regular values
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
