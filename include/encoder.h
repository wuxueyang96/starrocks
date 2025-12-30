#pragma once

#include <cstdint>
#include <roaring/roaring.hh>
#include <string>
#include <vector>

/**
 * Status codes for encoder operations
 */
enum class Status {
    OK = 0,           // Operation succeeded
    INVALID_INPUT,    // Invalid input data
    BUFFER_TOO_SMALL, // Output buffer too small
    OUT_OF_MEMORY,    // Memory allocation failed
    CORRUPTED_DATA,   // Data corruption detected
    UNKNOWN_ERROR     // Unknown error
};

inline const char* status_to_string(Status status) {
    switch (status) {
    case Status::OK:
        return "OK";
    case Status::INVALID_INPUT:
        return "Invalid input";
    case Status::BUFFER_TOO_SMALL:
        return "Buffer too small";
    case Status::OUT_OF_MEMORY:
        return "Out of memory";
    case Status::CORRUPTED_DATA:
        return "Corrupted data";
    case Status::UNKNOWN_ERROR:
        return "Unknown error";
    default:
        return "Unknown status";
    }
}

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
 * Provides a common interface for encoding unsigned 32-bit integers
 */
class Encoder {
public:
    virtual ~Encoder() = default;

    /**
     * Encode a Roaring bitmap
     * @param roaring The Roaring bitmap to encode
     * @param result Output buffer for encoded data
     * @return Status code
     */
    virtual Status encode(const roaring::Roaring& roaring, std::vector<uint8_t>* result) = 0;

    /**
     * Encode a single uint32 value
     * @param value The value to encode
     * @param result Output buffer for encoded data
     * @return Status code
     */
    virtual Status encode(uint32_t value, std::vector<uint8_t>* result) = 0;

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
