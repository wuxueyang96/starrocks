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

uint32_t VarIntEncoder::decodeValue(const uint8_t** input) {
    if (input == nullptr || *input == nullptr) {
        throw std::runtime_error("Invalid offset in decodeVarInt");
    }

    uint32_t result = 0;
    int shift = 0;

    while (*input != nullptr) {
        const uint8_t byte = **input;
        (*input)++;

        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            break;
        }
        shift += 7;
        if (shift > 28) {
            throw std::runtime_error("VarInt too large");
        }
    }
    return result;
}

uint32_t VarIntEncoder::decodeValue(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset >= data.size()) {
        throw std::runtime_error("Invalid offset in decodeVarInt");
    }

    uint32_t result = 0;
    int shift = 0;

    while (offset < data.size()) {
        const uint8_t byte = data[offset++];
        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            break;
        }
        shift += 7;
        if (shift > 28) {
            throw std::runtime_error("VarInt too large");
        }
    }
    return result;
}

std::vector<uint8_t> VarIntEncoder::encode(const std::vector<uint32_t>& values, const size_t start, const size_t end) {
    if (values.empty() || start >= end || end > values.size()) {
        return {};
    }

    std::vector<uint8_t> encoded;
    // Encode length first.
    encodeValue(end - start, encoded);
    // Encode first position
    encodeValue(values[start], encoded);
    // Encode deltas
    for (size_t i = start + 1; i < end; ++i) {
        const uint32_t delta = values[i] - values[i - 1];
        encodeValue(delta, encoded);
    }
    return encoded;
}

std::vector<uint32_t> VarIntEncoder::decode(const uint8_t* encoded, size_t size) {
    if (encoded == nullptr) {
        return {};
    }

    uint32_t length = decodeValue(&encoded);

    std::vector<uint32_t> positions;
    positions.reserve(length);

    uint32_t current_position = decodeValue(&encoded);
    positions.push_back(current_position);

    while (--length > 0) {
        const uint32_t delta = decodeValue(&encoded);
        current_position += delta;
        positions.push_back(current_position);
    }
    return positions;
}
