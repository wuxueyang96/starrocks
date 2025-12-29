#include "varint_encoder.h"
#include <algorithm>
#include <stdexcept>

VarIntEncoder::VarIntEncoder() = default;

VarIntEncoder::~VarIntEncoder() = default;

void VarIntEncoder::encodeValue(uint32_t value, std::vector<uint8_t>& output) {
    while (value >= 0x80) {
        output.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    output.push_back(static_cast<uint8_t>(value & 0x7F));
}

Status VarIntEncoder::encode(const roaring::Roaring& roaring, std::vector<uint8_t>* result) {
    if (!result) {
        return Status::INVALID_INPUT;
    }
    
    if (roaring.isEmpty()) {
        result->clear();
        return Status::OK;
    }

    std::vector<uint32_t> values(roaring.cardinality());
    roaring.toUint32Array(values.data());

    // Encode first position
    encodeValue(values[0], *result);
    // Encode deltas
    for (size_t i = 1; i < values.size(); ++i) {
        const uint32_t delta = values[i] - values[i - 1];
        encodeValue(delta, *result);
    }
    return Status::OK;
}

Status VarIntEncoder::encode(uint32_t value, std::vector<uint8_t>* result) {
    if (!result) {
        return Status::INVALID_INPUT;
    }
    encodeValue(value, *result);
    return Status::OK;
}
