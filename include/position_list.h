#pragma once

#include <vector>

#include "encoder_factory.h"

class PositionList {
public:
    PositionList();
    ~PositionList();

    // Add a position to the list
    void addPosition(uint32_t position);

    // Encode the position list using specified encoding type
    // If encoding_type is ADAPTIVE, it will choose the best encoding automatically
    // All position lists are encoded in blocks with default block size of 128
    std::vector<uint8_t> encode(const EncodingType& encoding_type) const;

    // Decode from compressed bytes back to positions
    void decode(const std::vector<uint8_t>& encoded_data, const EncodingType& encoding_type);

    // Get all positions
    const std::vector<uint32_t>& getPositions() const;

    // Get the number of positions
    size_t size() const;

    // Clear all positions
    void clear();

private:
    static constexpr size_t BLOCK_SIZE = 128; // Fixed block size for chunking
    std::vector<uint32_t> positions_;

    // Encode positions using block-based approach
    std::vector<uint8_t> encodeWithBlocks(const std::shared_ptr<Encoder>& encoder) const;
};
