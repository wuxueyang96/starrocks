#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class EncodingType {
    ADAPTIVE = 0,      // Automatically choose best encoding
    VARINT = 1,        // Variable-length integer encoding
    FOR_VARINT = 2,    // Frame of Reference + VarInt for exceptions
    PFOR_DELTA = 3,    // Patched Frame of Reference with delta encoding
    SIMPLE9 = 4,       // Simple9 encoding
    NEW_PFOR_DELTA = 5 // NewPForDelta with Simple9 for exceptions
};

inline std::string get_encoding_type_name(EncodingType encoding) {
    switch (encoding) {
    case EncodingType::ADAPTIVE:
        return "ADAPTIVE";
    case EncodingType::VARINT:
        return "VARINT";
    case EncodingType::FOR_VARINT:
        return "FOR_VARINT";
    case EncodingType::PFOR_DELTA:
        return "PFOR_DELTA";
    case EncodingType::SIMPLE9:
        return "SIMPLE9";
    case EncodingType::NEW_PFOR_DELTA:
        return "NEW_PFOR_DELTA";
    default:
        return "UNKNOWN";
    }
}

/**
 * Abstract base class for all integer encoders
 * Provides a common interface for encoding and decoding unsigned 32-bit integers
 */
class Encoder {
public:
    virtual ~Encoder() = default;

    /**
     * Encode a list of unsigned 32-bit integers in range [start, end)
     * @param values The values to encode
     * @param start The start index of the values to encode
     * @param end The end index of the values to encode
     * @return Encoded byte array
     */
    virtual std::vector<uint8_t> encode(const std::vector<uint32_t>& values, size_t start, size_t end) = 0;

    /**
     * Decode a byte array back to unsigned 32-bit integers
     * @param encoded The encoded byte array
     * @return Decoded values
     */
    virtual std::vector<uint32_t> decode(const uint8_t* encoded, size_t size) = 0;

    /**
     * Get the encoding type of this encoder
     * @return The encoding type
     */
    virtual EncodingType getType() const = 0;

    /**
     * Get a human-readable name for this encoder
     * @return The encoder name
     */
    virtual const char* getName() const = 0;
};
