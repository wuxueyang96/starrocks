#pragma once

#include "encoder.h"

/**
 * VarInt (Variable-length integer) encoder
 * Uses 7 bits for data and 1 bit as continuation flag
 * Efficient for small integer values
 */
class VarIntEncoder final : public Encoder {
public:
    VarIntEncoder();
    ~VarIntEncoder() override;

    std::vector<uint8_t> encode(const std::vector<uint32_t>& values, size_t start, size_t end) override;
    std::vector<uint32_t> decode(const uint8_t* encoded, size_t size) override;
    EncodingType getType() const override { return EncodingType::VARINT; }
    const char* getName() const override { return "VarInt"; }

    // Helper methods for encoding/decoding single values
    static void encodeValue(uint32_t value, std::vector<uint8_t>& output);
    static uint32_t decodeValue(const uint8_t** input);
    static uint32_t decodeValue(const std::vector<uint8_t>& data, size_t& offset);
};
