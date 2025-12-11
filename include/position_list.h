#pragma once

#include <vector>
#include <string>
#include "encoding_util.h"

class PositionList {
public:
    PositionList();
    ~PositionList();

    // Add a position to the list
    void addPosition(uint32_t position);

    // Encode the position list using specified encoding type
    std::vector<uint8_t> encode(EncodingType encoding_type = EncodingType::FOR_VARINT) const;

    // Decode from compressed bytes back to positions
    void decode(const std::vector<uint8_t>& encoded_data, EncodingType encoding_type = EncodingType::FOR_VARINT);

    // Get all positions
    const std::vector<uint32_t>& getPositions() const;

    // Get the number of positions
    size_t size() const;

    // Clear all positions
    void clear();

    // Get compressed size (after encoding)
    size_t getCompressedSize(EncodingType encoding_type = EncodingType::FOR_VARINT) const;

    // Get uncompressed size
    size_t getUncompressedSize() const;

private:
    std::vector<uint32_t> positions_;
};
