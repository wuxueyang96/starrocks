#include "position_list.h"

#include <algorithm>
#include <stdexcept>

PositionList::PositionList() = default;

PositionList::~PositionList() = default;

void PositionList::addPosition(const uint32_t position) {
    positions_.push_back(position);
}

std::vector<uint8_t> PositionList::encode(EncodingType encoding_type, EncodingType& actual_encoding,
                                          const BlockEncodingConfig* block_config) const {
    if (positions_.empty()) {
        actual_encoding = EncodingType::VARINT;
        return std::vector<uint8_t>();
    }

    // If block encoding is enabled and configured, use block-based approach
    if (block_config != nullptr && block_config->enable_block_encoding &&
        positions_.size() > block_config->block_size) {
        return encodeWithBlocks(encoding_type, *block_config);
    }

    // Regular single-block encoding
    switch (encoding_type) {
    case EncodingType::VARINT:
        actual_encoding = EncodingType::VARINT;
        return EncodingUtil::encodeDeltaVarInt(positions_);
    case EncodingType::FOR_VARINT:
        actual_encoding = EncodingType::FOR_VARINT;
        return EncodingUtil::encodeFrameOfReference(positions_);
    case EncodingType::PFOR_DELTA:
        actual_encoding = EncodingType::PFOR_DELTA;
        return EncodingUtil::encodePForDelta(positions_);
    case EncodingType::ADAPTIVE:
        // Use heuristic-based selection for speed
        return EncodingUtil::encodeAdaptive(positions_, actual_encoding);
    default:
        actual_encoding = EncodingType::VARINT;
        return EncodingUtil::encodeDeltaVarInt(positions_);
    }
}

void PositionList::decode(const std::vector<uint8_t>& encoded_data, EncodingType encoding_type) {
    positions_.clear();

    if (encoded_data.empty()) {
        return;
    }

    // Check if this is block-encoded data
    // Block-encoded data starts with a special marker: 0xFF
    if (encoded_data.size() > 0 && encoded_data[0] == 0xFF) {
        // Block-encoded format
        size_t offset = 1;

        // Decode number of blocks
        if (offset >= encoded_data.size()) {
            throw std::runtime_error("Invalid block-encoded data: missing block count");
        }
        uint32_t num_blocks = EncodingUtil::decodeVarInt(encoded_data, offset);

        // Decode each block
        for (uint32_t i = 0; i < num_blocks; ++i) {
            // Read encoding type for this block
            if (offset >= encoded_data.size()) {
                throw std::runtime_error("Invalid block-encoded data: missing encoding type");
            }
            EncodingType block_encoding = static_cast<EncodingType>(encoded_data[offset++]);

            // Read block size
            uint32_t block_size = EncodingUtil::decodeVarInt(encoded_data, offset);

            // Extract block data
            if (offset + block_size > encoded_data.size()) {
                throw std::runtime_error("Invalid block-encoded data: block size exceeds data");
            }
            std::vector<uint8_t> block_data(encoded_data.begin() + offset, encoded_data.begin() + offset + block_size);
            offset += block_size;

            // Decode block based on its encoding type
            std::vector<uint32_t> block_positions;
            switch (block_encoding) {
            case EncodingType::VARINT:
                block_positions = EncodingUtil::decodeDeltaVarInt(block_data);
                break;
            case EncodingType::FOR_VARINT:
                block_positions = EncodingUtil::decodeFrameOfReference(block_data);
                break;
            case EncodingType::PFOR_DELTA:
                block_positions = EncodingUtil::decodePForDelta(block_data);
                break;
            default:
                block_positions = EncodingUtil::decodeDeltaVarInt(block_data);
                break;
            }

            // Append to positions
            positions_.insert(positions_.end(), block_positions.begin(), block_positions.end());
        }
        return;
    }

    // Regular single-block decoding
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

size_t PositionList::getCompressedSize(EncodingType encoding_type, EncodingType& actual_encoding,
                                       const BlockEncodingConfig* block_config) const {
    return encode(encoding_type, actual_encoding, block_config).size();
}

size_t PositionList::getUncompressedSize() const {
    return positions_.size() * sizeof(uint32_t);
}

// Encode positions using block-based approach
std::vector<uint8_t> PositionList::encodeWithBlocks(EncodingType encoding_type,
                                                    const BlockEncodingConfig& config) const {
    std::vector<uint8_t> encoded;

    // Sort positions for delta encoding
    std::vector<uint32_t> sorted_positions = positions_;
    std::sort(sorted_positions.begin(), sorted_positions.end());

    // Block marker: 0xFF indicates block-encoded data
    encoded.push_back(0xFF);

    // Calculate number of blocks
    const size_t num_blocks = (sorted_positions.size() + config.block_size - 1) / config.block_size;
    EncodingUtil::encodeVarInt(static_cast<uint32_t>(num_blocks), encoded);

    // Encode each block
    for (size_t block_idx = 0; block_idx < num_blocks; ++block_idx) {
        const size_t start_idx = block_idx * config.block_size;
        const size_t end_idx = std::min(start_idx + config.block_size, sorted_positions.size());

        // Extract block
        std::vector<uint32_t> block(sorted_positions.begin() + start_idx, sorted_positions.begin() + end_idx);

        // Choose encoding for this block
        EncodingType block_encoding = encoding_type;
        std::vector<uint8_t> block_encoded;

        if (encoding_type == EncodingType::ADAPTIVE) {
            // Let adaptive encoding choose the best for this block
            block_encoded = EncodingUtil::encodeAdaptive(block, block_encoding);
        } else {
            // Use specified encoding
            block_encoding = encoding_type;
            switch (encoding_type) {
            case EncodingType::VARINT:
                block_encoded = EncodingUtil::encodeDeltaVarInt(block);
                break;
            case EncodingType::FOR_VARINT:
                block_encoded = EncodingUtil::encodeFrameOfReference(block);
                break;
            case EncodingType::PFOR_DELTA:
                block_encoded = EncodingUtil::encodePForDelta(block);
                break;
            default:
                block_encoded = EncodingUtil::encodeDeltaVarInt(block);
                break;
            }
        }

        // Store encoding type for this block
        encoded.push_back(static_cast<uint8_t>(block_encoding));

        // Store block size and data
        EncodingUtil::encodeVarInt(static_cast<uint32_t>(block_encoded.size()), encoded);
        encoded.insert(encoded.end(), block_encoded.begin(), block_encoded.end());
    }

    return encoded;
}
