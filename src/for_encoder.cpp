#include "for_encoder.h"
#include "varint_encoder.h"
#include <algorithm>

std::vector<uint8_t> FrameOfReferenceEncoder::encode(const std::vector<uint32_t>& values, const size_t start, const size_t end) {
    if (values.empty() || start >= end || start > values.size() || end > values.size()) {
        return {};
    }

    std::vector<uint8_t> encoded;

    if (end - start == 1) {
        VarIntEncoder::encodeValue(values[start], encoded);
        return encoded;
    }

    // Use the minimum value as the base (frame)
    const uint32_t base = values[start];
    VarIntEncoder::encodeValue(base, encoded);

    // Calculate differences from base
    std::vector<uint32_t> deltas;
    deltas.reserve(end - start - 1);

    uint32_t max_delta = 0;
    for (size_t i = start + 1; i < end; ++i) {
        const uint32_t delta = values[i] - base;
        deltas.push_back(delta);
        max_delta = std::max(max_delta, delta);
    }

    // Determine bits needed per delta
    const uint8_t bits_per_value = max_delta == 0 ? 0 : (32 - __builtin_clz(max_delta));
    encoded.push_back(bits_per_value);

    if (bits_per_value == 0) {
        // All values are the same
        return encoded;
    }

    // Bit-pack the deltas
    size_t bit_pos = 0;
    uint64_t buffer = 0;

    for (const uint32_t delta : deltas) {
        buffer |= (static_cast<uint64_t>(delta) << bit_pos);
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

std::vector<uint32_t> FrameOfReferenceEncoder::decode(const uint8_t* encoded, size_t size) {
    if (encoded == nullptr || size == 0) {
        return {};
    }

    const uint8_t* ptr = encoded;
    const uint8_t* end_ptr = encoded + size;

    // Decode base value
    const uint32_t base = VarIntEncoder::decodeValue(&ptr);
    
    // If only base value (single value case)
    if (ptr >= end_ptr) {
        return {base};
    }

    std::vector<uint32_t> values;
    values.push_back(base);

    // Decode bits per value
    const uint8_t bits_per_value = *ptr++;

    if (bits_per_value == 0) {
        // All values are the same - but we don't know the count
        // This is a problem - we need to store count or calculate from remaining size
        // For now, return just the base value
        return values;
    }

    // Calculate how many values we can decode from remaining data
    const size_t remaining_bytes = end_ptr - ptr;
    const size_t total_bits = remaining_bytes * 8;
    const size_t num_deltas = total_bits / bits_per_value;
    
    values.reserve(num_deltas + 1);

    // Unpack deltas
    uint64_t buffer = 0;
    size_t buffered_bits = 0;

    for (size_t i = 0; i < num_deltas && ptr < end_ptr; ++i) {
        // Ensure we have enough bits in buffer
        while (buffered_bits < bits_per_value && ptr < end_ptr) {
            buffer |= (static_cast<uint64_t>(*ptr++) << buffered_bits);
            buffered_bits += 8;
        }

        if (buffered_bits < bits_per_value) {
            break;
        }

        // Extract delta
        const uint64_t mask = (1ULL << bits_per_value) - 1;
        const uint32_t delta = static_cast<uint32_t>(buffer & mask);
        values.push_back(base + delta);

        buffer >>= bits_per_value;
        buffered_bits -= bits_per_value;
    }

    return values;
}
