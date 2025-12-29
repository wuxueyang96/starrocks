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

    Status encode(const roaring::Roaring& roaring, std::vector<uint8_t>* result) override;
    Status encode(uint32_t value, std::vector<uint8_t>* result) override;
    EncodingType getType() const override { return EncodingType::VARINT; }
    const char* getName() const override { return "VarInt"; }

    // Helper method for encoding single values
    static void encodeValue(uint32_t value, std::vector<uint8_t>& output);
};
