#include "position_list.h"

#include <algorithm>

#include "encoder_factory.h"
#include "varint_encoder.h"

PositionList::PositionList() = default;

PositionList::~PositionList() = default;

void PositionList::addPosition(const uint32_t position) {
    positions_.push_back(position);
}

std::vector<uint8_t> PositionList::encode(const EncodingType& encoding_type, const BlockEncodingConfig* block_config) const {
    const auto encoder = EncoderFactory::createEncoder(encoding_type);
    // If block encoding is enabled and configured, use block-based approach
    if (block_config != nullptr && block_config->enable_block_encoding) {
        return encodeWithBlocks(encoder, *block_config);
    }
    return encoder->encode(positions_, 0, positions_.size());
}

void PositionList::decode(const std::vector<uint8_t>& encoded_data, const EncodingType& encoding_type) {
    const auto encoder = EncoderFactory::createEncoder(encoding_type);

    positions_.clear();

    if (encoded_data.empty()) {
        return;
    }

    // Check if this is block-encoded data
    // Block-encoded data starts with a special marker: 0xFF
    if (encoded_data[0] != 0xFF) {
        positions_ = encoder->decode(encoded_data.data());
        return;
    }

    auto ptr = encoded_data.data() + 1; // skip block-encoded marker
    uint32_t num_blocks = VarIntEncoder::decodeValue(&ptr);
    while (num_blocks > 0) {
        // Block-encoded format
        auto decoded_block = encoder->decode(ptr);
        positions_.insert(positions_.end(), decoded_block.begin(), decoded_block.end());
        --num_blocks;
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

// Encode positions using block-based approach
std::vector<uint8_t> PositionList::encodeWithBlocks(const std::shared_ptr<Encoder>& encoder,
                                                    const BlockEncodingConfig& config) const {
    std::vector<uint8_t> encoded;

    // Sort positions for delta encoding
    std::vector<uint32_t> sorted_positions = positions_;
    std::sort(sorted_positions.begin(), sorted_positions.end());

    // Block marker: 0xFF indicates block-encoded data
    encoded.push_back(0xFF);

    // Calculate number of blocks
    const size_t num_blocks = (sorted_positions.size() + config.block_size - 1) / config.block_size;
    VarIntEncoder::encodeValue(static_cast<uint32_t>(num_blocks), encoded);

    // Encode each block
    for (size_t block_idx = 0; block_idx < num_blocks; ++block_idx) {
        const size_t start_idx = block_idx * config.block_size;
        const size_t end_idx = std::min(start_idx + config.block_size, sorted_positions.size());

        // Extract block
        std::vector<uint8_t> encoded_block = encoder->encode(sorted_positions, start_idx, end_idx);
        encoded.insert(encoded.end(), encoded_block.begin(), encoded_block.end());
    }
    return encoded;
}
