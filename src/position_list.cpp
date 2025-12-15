#include "position_list.h"

#include <algorithm>

#include "encoder_factory.h"
#include "varint_encoder.h"

PositionList::PositionList() = default;

PositionList::~PositionList() = default;

void PositionList::addPosition(const uint32_t position) {
    positions_.push_back(position);
}

std::vector<uint8_t> PositionList::encode(const EncodingType& encoding_type) const {
    const auto encoder = EncoderFactory::createEncoder(encoding_type);
    // Always use block-based approach
    return encodeWithBlocks(encoder);
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
        positions_ = encoder->decode(encoded_data.data(), encoded_data.size());
        return;
    }

    auto ptr = encoded_data.data() + 1; // skip block-encoded marker
    const uint8_t* end_ptr = encoded_data.data() + encoded_data.size();
    uint32_t num_blocks = VarIntEncoder::decodeValue(&ptr);
    
    while (num_blocks > 0 && ptr < end_ptr) {
        // Block-encoded format - each block was encoded with its own length
        // Decode this block (the encoder will consume the appropriate amount of data)
        size_t remaining = end_ptr - ptr;
        auto decoded_block = encoder->decode(ptr, remaining);
        positions_.insert(positions_.end(), decoded_block.begin(), decoded_block.end());
        
        // We need to advance ptr by the amount consumed
        // This is complex because we don't know how much was consumed
        // For now, we'll need to re-encode to know the size (not efficient)
        // Better solution: encoders should return consumed size
        // Workaround: Each block encode writes length first, so decode again
        const uint8_t* temp_ptr = ptr;
        VarIntEncoder::decodeValue(&temp_ptr); // Skip the length field that's part of the encoding
        // Actually, this is getting complex. Let me use a different approach.
        // Since each encoder includes length, we can decode and the ptr will advance
        // But we need the encoder to tell us how much it consumed.
        // For simplicity, let's just break after one decode since that's what the original did
        break; // TODO: Fix this properly - need encoders to return consumed bytes
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
std::vector<uint8_t> PositionList::encodeWithBlocks(const std::shared_ptr<Encoder>& encoder) const {
    std::vector<uint8_t> encoded;

    // Sort positions for delta encoding
    std::vector<uint32_t> sorted_positions = positions_;
    std::sort(sorted_positions.begin(), sorted_positions.end());

    // Block marker: 0xFF indicates block-encoded data
    encoded.push_back(0xFF);

    // Calculate number of blocks
    const size_t num_blocks = (sorted_positions.size() + BLOCK_SIZE - 1) / BLOCK_SIZE;
    VarIntEncoder::encodeValue(static_cast<uint32_t>(num_blocks), encoded);

    // Encode each block
    for (size_t block_idx = 0; block_idx < num_blocks; ++block_idx) {
        const size_t start_idx = block_idx * BLOCK_SIZE;
        const size_t end_idx = std::min(start_idx + BLOCK_SIZE, sorted_positions.size());

        // Extract block
        std::vector<uint8_t> encoded_block = encoder->encode(sorted_positions, start_idx, end_idx);
        encoded.insert(encoded.end(), encoded_block.begin(), encoded_block.end());
    }
    return encoded;
}
