#include "position_list.h"

#include <algorithm>
#include <stdexcept>

PositionList::PositionList() = default;

PositionList::~PositionList() = default;

void PositionList::addPosition(const uint32_t position) {
    positions_.push_back(position);
}

std::vector<uint8_t> PositionList::encode() const {
    std::vector<uint8_t> encoded;
    if (positions_.empty()) {
        return encoded;
    }

    // First, sort positions to ensure they are in ascending order
    std::vector<uint32_t> sorted_positions = positions_;
    std::sort(sorted_positions.begin(), sorted_positions.end());

    // Encode the first position directly (no delta for the first element)
    encodeVarInt(sorted_positions[0], encoded);

    // Encode subsequent positions using delta encoding
    for (size_t i = 1; i < sorted_positions.size(); ++i) {
        const uint32_t delta = sorted_positions[i] - sorted_positions[i - 1];
        encodeVarInt(delta, encoded);
    }
    return encoded;
}

void PositionList::decode(const std::vector<uint8_t>& encoded_data) {
    positions_.clear();

    if (encoded_data.empty()) {
        return;
    }
    size_t offset = 0;
    // Decode the first position
    uint32_t current_position = decodeVarInt(encoded_data, offset);
    positions_.push_back(current_position);

    // Decode subsequent positions by adding deltas
    while (offset < encoded_data.size()) {
        const uint32_t delta = decodeVarInt(encoded_data, offset);
        current_position += delta;
        positions_.push_back(current_position);
    }
}

const std::vector<uint32_t>& PositionList::getPositions() const {
    return positions_;
}

size_t PositionList::size() const {
    return positions_.size();
}

void PositionList::clear() {
    positions_.clear();
}

size_t PositionList::getCompressedSize() const {
    return encode().size();
}

size_t PositionList::getUncompressedSize() const {
    return positions_.size() * sizeof(uint32_t);
}

// VByte (Variable Byte) encoding implementation
// Each byte uses 7 bits for data and 1 bit as continuation flag
// If the high bit is 1, more bytes follow; if 0, this is the last byte
void PositionList::encodeVarInt(uint32_t value, std::vector<uint8_t>& output) {
    while (value >= 0x80) {
        // Set the continuation bit (high bit = 1) and output lower 7 bits
        output.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    // Last byte: high bit = 0
    output.push_back(static_cast<uint8_t>(value & 0x7F));
}

uint32_t PositionList::decodeVarInt(const std::vector<uint8_t>& input, size_t& offset) {
    if (offset >= input.size()) {
        throw std::runtime_error("Invalid offset in decodeVarInt");
    }

    uint32_t result = 0;
    int shift = 0;

    while (offset < input.size()) {
        const uint8_t byte = input[offset++];
        
        // Extract the lower 7 bits and add to result
        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        
        // If high bit is 0, this is the last byte
        if ((byte & 0x80) == 0) {
            break;
        }
        
        shift += 7;
        
        // Prevent overflow (max 5 bytes for uint32_t)
        if (shift > 28) {
            throw std::runtime_error("VarInt too large");
        }
    }

    return result;
}
