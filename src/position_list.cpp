#include "position_list.h"

#include <algorithm>
#include <roaring/roaring.hh>

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

    // Convert to Roaring bitmap and encode
    roaring::Roaring roaring(sorted_positions.size(), sorted_positions.data());
    const auto encoder = EncoderFactory::createEncoder(encoding_type);
    return encoder->encode(roaring);
}

void PositionList::decode(const std::vector<uint8_t>& encoded_data, const EncodingType& encoding_type) {
    positions_.clear();

    if (encoded_data.empty()) {
        return;
    }

    // Decode to Roaring bitmap and extract positions
    const auto encoder = EncoderFactory::createEncoder(encoding_type);
    roaring::Roaring roaring = encoder->decode(encoded_data);
    
    positions_.resize(roaring.cardinality());
    roaring.toUint32Array(positions_.data());
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
