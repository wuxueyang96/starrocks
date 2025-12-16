#include "position_list.h"

#include <algorithm>

#include "encoder_factory.h"

PositionList::PositionList() = default;

PositionList::~PositionList() = default;

void PositionList::addPosition(const uint32_t position) {
    positions_.push_back(position);
}

std::vector<uint8_t> PositionList::encode(const EncodingType& encoding_type) const {
    if (positions_.empty()) {
        return {};
    }

    // Sort positions for delta encoding
    std::vector<uint32_t> sorted_positions = positions_;
    std::sort(sorted_positions.begin(), sorted_positions.end());

    // Directly encode all positions without blocks
    const auto encoder = EncoderFactory::createEncoder(encoding_type);
    return encoder->encode(sorted_positions, 0, sorted_positions.size());
}

void PositionList::decode(const std::vector<uint8_t>& encoded_data, const EncodingType& encoding_type) {
    positions_.clear();

    if (encoded_data.empty()) {
        return;
    }

    // Directly decode all positions without blocks
    const auto encoder = EncoderFactory::createEncoder(encoding_type);
    positions_ = encoder->decode(encoded_data.data(), encoded_data.size());
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
