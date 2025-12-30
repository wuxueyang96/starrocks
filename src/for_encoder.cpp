#include "for_encoder.h"

#include <algorithm>
#include <stdexcept>

#include "varint_encoder.h"

Status FrameOfReferenceEncoder::encode(const roaring::Roaring& roaring, std::vector<uint8_t>* result) {
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

    // Encode the count (needed for FOR due to bit-packing alignment)
    VarIntEncoder::encodeValue(static_cast<uint32_t>(end - start), *result);

    if (end - start == 1) {
        VarIntEncoder::encodeValue(values[start], *result);
        return Status::OK;
    }

    // Use the minimum value as the base (frame)
    const uint32_t base = values[start];
    VarIntEncoder::encodeValue(base, *result);

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
    result->push_back(bits_per_value);

    if (bits_per_value == 0) {
        // All values are the same
        return Status::OK;
    }

    // Bit-pack the deltas
    size_t bit_pos = 0;
    uint64_t buffer = 0;

    for (const uint32_t delta : deltas) {
        buffer |= (static_cast<uint64_t>(delta) << bit_pos);
        bit_pos += bits_per_value;

        while (bit_pos >= 8) {
            result->push_back(static_cast<uint8_t>(buffer & 0xFF));
            buffer >>= 8;
            bit_pos -= 8;
        }
    }

    // Flush remaining bits
    if (bit_pos > 0) {
        result->push_back(static_cast<uint8_t>(buffer & 0xFF));
    }

    return Status::OK;
}

Status FrameOfReferenceEncoder::encode(uint32_t value, std::vector<uint8_t>* result) {
    if (!result) {
        return Status::INVALID_INPUT;
    }
    VarIntEncoder::encodeValue(1, *result); // count = 1
    VarIntEncoder::encodeValue(value, *result);
    return Status::OK;
}
