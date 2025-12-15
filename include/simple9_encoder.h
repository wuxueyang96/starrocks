#pragma once

#include "encoder.h"

/**
 * Simple9 encoder
 * Packs multiple integers into a 32-bit word
 * Format: 4-bit selector + 28 bits of data
 * 9 different packing modes for optimal space usage
 */
class Simple9Encoder : public Encoder {
public:
    struct Simple9Mode {
        uint8_t count;      // Number of integers in this mode
        uint8_t bits;       // Bits per integer
        uint32_t max_value; // Maximum value that can be encoded
    };

    // Simple9 modes (9 different packing modes)
    static constexpr Simple9Mode MODES[9] = {
        {28, 1, 1},        // Mode 0: 28 integers, 1 bit each, max=1
        {14, 2, 3},        // Mode 1: 14 integers, 2 bits each, max=3
        {9, 3, 7},         // Mode 2: 9 integers, 3 bits each, max=7
        {7, 4, 15},        // Mode 3: 7 integers, 4 bits each, max=15
        {5, 5, 31},        // Mode 4: 5 integers, 5 bits each, max=31
        {4, 7, 127},       // Mode 5: 4 integers, 7 bits each, max=127
        {3, 9, 511},       // Mode 6: 3 integers, 9 bits each, max=511
        {2, 14, 16383},    // Mode 7: 2 integers, 14 bits each, max=16383
        {1, 28, 268435455} // Mode 8: 1 integer, 28 bits, max=268435455
    };

    std::vector<uint8_t> encode(const std::vector<uint32_t>& values, size_t start, size_t end) override;
    std::vector<uint32_t> decode(const uint8_t* encoded, size_t size) override;
    EncodingType getType() const override { return EncodingType::SIMPLE9; }
    const char* getName() const override { return "Simple9"; }

    // Helper method for batch encoding (used by other encoders)
    static size_t encodeBatch(const std::vector<uint32_t>& values, size_t start_idx,
                              std::vector<uint8_t>& output);

private:
    std::vector<uint8_t> encodeImpl(const std::vector<uint32_t>& values);
    std::vector<uint32_t> decodeImpl(const std::vector<uint8_t>& encoded);
};
