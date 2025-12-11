#include "position_list.h"

#include <algorithm>
#include <stdexcept>

PositionList::PositionList() = default;

PositionList::~PositionList() = default;

void PositionList::addPosition(const uint32_t position) {
    positions_.push_back(position);
}

std::vector<uint8_t> PositionList::encode(EncodingType encoding_type) const {
    if (positions_.empty()) {
        return std::vector<uint8_t>();
    }

    switch (encoding_type) {
        case EncodingType::VARINT:
            return EncodingUtil::encodeDeltaVarInt(positions_);
        case EncodingType::FOR_VARINT:
            return EncodingUtil::encodeFrameOfReference(positions_);
        case EncodingType::PFOR_DELTA:
            return EncodingUtil::encodePForDelta(positions_);
        default:
            return EncodingUtil::encodeDeltaVarInt(positions_);
    }
}

void PositionList::decode(const std::vector<uint8_t>& encoded_data, EncodingType encoding_type) {
    positions_.clear();

    if (encoded_data.empty()) {
        return;
    }

    switch (encoding_type) {
        case EncodingType::VARINT:
            positions_ = EncodingUtil::decodeDeltaVarInt(encoded_data);
            break;
        case EncodingType::FOR_VARINT:
            positions_ = EncodingUtil::decodeFrameOfReference(encoded_data);
            break;
        case EncodingType::PFOR_DELTA:
            positions_ = EncodingUtil::decodePForDelta(encoded_data);
            break;
        default:
            positions_ = EncodingUtil::decodeDeltaVarInt(encoded_data);
            break;
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

size_t PositionList::getCompressedSize(EncodingType encoding_type) const {
    return encode(encoding_type).size();
}

size_t PositionList::getUncompressedSize() const {
    return positions_.size() * sizeof(uint32_t);
}
