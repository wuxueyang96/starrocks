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

std::vector<uint8_t> VarIntEncoder::encode(const roaring::Roaring& roaring) {
    if (roaring.isEmpty()) {
        return {};
    }

    std::vector<uint8_t> encoded;
    std::vector<uint32_t> values(roaring.cardinality());
    roaring.toUint32Array(values.data());

    // Encode first position
    encodeValue(values[0], encoded);
    // Encode deltas
    for (size_t i = 1; i < values.size(); ++i) {
        const uint32_t delta = values[i] - values[i - 1];
        encodeValue(delta, encoded);
    }
    return encoded;
}

roaring::Roaring VarIntEncoder::decode(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return roaring::Roaring();
    }

    size_t offset = 0;
    std::vector<uint32_t> positions;
    uint32_t current_position = decodeValue(data, offset);
    positions.push_back(current_position);

    while (offset < data.size()) {
        const uint32_t delta = decodeValue(data, offset);
        current_position += delta;
        positions.push_back(current_position);
    }

    return roaring::Roaring(positions.size(), positions.data());
}
