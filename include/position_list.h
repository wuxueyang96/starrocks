#pragma once

#include <vector>
#include <string>

class PositionList {
public:
    PositionList();
    ~PositionList();

    // Add a position to the list
    void addPosition(uint32_t position);

    // Encode the position list using delta encoding and variable-length integers
    std::vector<uint8_t> encode() const;

    // Decode from compressed bytes back to positions
    void decode(const std::vector<uint8_t>& encoded_data);

    // Get all positions
    const std::vector<uint32_t>& getPositions() const;

    // Get the number of positions
    size_t size() const;

    // Clear all positions
    void clear();

    // Get compressed size (after encoding)
    size_t getCompressedSize() const;

    // Get uncompressed size
    size_t getUncompressedSize() const;

    // Variable-length integer encoding (VByte encoding) - made public for reuse
    static void encodeVarInt(uint32_t value, std::vector<uint8_t>& output);

    // Variable-length integer decoding
    static uint32_t decodeVarInt(const std::vector<uint8_t>& input, size_t& offset);

private:
    std::vector<uint32_t> positions_;
};
