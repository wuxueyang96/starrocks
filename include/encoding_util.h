#pragma once

#include <vector>
#include <cstdint>
#include <stdexcept>
#include <algorithm>

enum class EncodingType {
    VARINT = 0,      // Variable-length integer encoding
    FOR_VARINT = 1,  // Frame of Reference + VarInt for exceptions
    PFOR_DELTA = 2   // Patched Frame of Reference with delta encoding
};

class EncodingUtil {
public:
    // Variable-length integer encoding (VByte encoding)
    static void encodeVarInt(uint32_t value, std::vector<uint8_t>& output) {
        while (value >= 0x80) {
            output.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
            value >>= 7;
        }
        output.push_back(static_cast<uint8_t>(value & 0x7F));
    }

    // Variable-length integer decoding
    static uint32_t decodeVarInt(const std::vector<uint8_t>& input, size_t& offset) {
        if (offset >= input.size()) {
            throw std::runtime_error("Invalid offset in decodeVarInt");
        }

        uint32_t result = 0;
        int shift = 0;

        while (offset < input.size()) {
            const uint8_t byte = input[offset++];
            result |= static_cast<uint32_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) {
                break;
            }
            shift += 7;
            if (shift > 28) {
                throw std::runtime_error("VarInt too large");
            }
        }

        return result;
    }

    // Frame of Reference encoding for sorted integers
    // Uses a base value and encodes differences, with bit-packing for efficiency
    static std::vector<uint8_t> encodeFrameOfReference(const std::vector<uint32_t>& values) {
        std::vector<uint8_t> encoded;
        
        if (values.empty()) {
            return encoded;
        }

        // Encode the count
        encodeVarInt(static_cast<uint32_t>(values.size()), encoded);

        if (values.size() == 1) {
            encodeVarInt(values[0], encoded);
            return encoded;
        }

        // Sort values (should already be sorted, but ensure it)
        std::vector<uint32_t> sorted_values = values;
        std::sort(sorted_values.begin(), sorted_values.end());

        // Use the minimum value as the base (frame)
        const uint32_t base = sorted_values[0];
        encodeVarInt(base, encoded);

        // Calculate differences from base
        std::vector<uint32_t> deltas;
        deltas.reserve(sorted_values.size() - 1);
        
        uint32_t max_delta = 0;
        for (size_t i = 1; i < sorted_values.size(); ++i) {
            const uint32_t delta = sorted_values[i] - base;
            deltas.push_back(delta);
            max_delta = std::max(max_delta, delta);
        }

        // Determine bits needed per delta
        const uint8_t bits_per_value = max_delta == 0 ? 0 : (32 - __builtin_clz(max_delta));
        encoded.push_back(bits_per_value);

        if (bits_per_value == 0) {
            // All values are the same
            return encoded;
        }

        // Bit-pack the deltas
        size_t bit_pos = 0;
        uint64_t buffer = 0;
        
        for (const uint32_t delta : deltas) {
            buffer |= (static_cast<uint64_t>(delta) << bit_pos);
            bit_pos += bits_per_value;
            
            while (bit_pos >= 8) {
                encoded.push_back(static_cast<uint8_t>(buffer & 0xFF));
                buffer >>= 8;
                bit_pos -= 8;
            }
        }
        
        // Flush remaining bits
        if (bit_pos > 0) {
            encoded.push_back(static_cast<uint8_t>(buffer & 0xFF));
        }

        return encoded;
    }

    // Decode Frame of Reference encoded data
    static std::vector<uint32_t> decodeFrameOfReference(const std::vector<uint8_t>& encoded) {
        std::vector<uint32_t> values;
        
        if (encoded.empty()) {
            return values;
        }

        size_t offset = 0;
        
        // Decode count
        const uint32_t count = decodeVarInt(encoded, offset);
        if (count == 0) {
            return values;
        }

        values.reserve(count);

        // Decode base value
        const uint32_t base = decodeVarInt(encoded, offset);
        values.push_back(base);

        if (count == 1) {
            return values;
        }

        // Decode bits per value
        if (offset >= encoded.size()) {
            throw std::runtime_error("Invalid FOR encoding: missing bits_per_value");
        }
        const uint8_t bits_per_value = encoded[offset++];

        if (bits_per_value == 0) {
            // All values are the same
            values.resize(count, base);
            return values;
        }

        // Unpack deltas
        size_t bit_pos = 0;
        uint64_t buffer = 0;
        size_t buffered_bits = 0;
        
        for (size_t i = 1; i < count; ++i) {
            // Ensure we have enough bits in buffer
            while (buffered_bits < bits_per_value && offset < encoded.size()) {
                buffer |= (static_cast<uint64_t>(encoded[offset++]) << buffered_bits);
                buffered_bits += 8;
            }
            
            // Extract delta
            const uint64_t mask = (1ULL << bits_per_value) - 1;
            const uint32_t delta = static_cast<uint32_t>(buffer & mask);
            values.push_back(base + delta);
            
            buffer >>= bits_per_value;
            buffered_bits -= bits_per_value;
        }

        return values;
    }

    // PForDelta encoding: Patched Frame of Reference with delta encoding
    // More efficient for datasets with outliers
    // Algorithm:
    // 1. Use 90th percentile to determine normal bit width
    // 2. Store exceptions (outliers) separately with their indices
    // 3. Bit-pack normal values efficiently
    // 4. Exceptions are encoded using VarInt
    // This is more space-efficient than FOR when data has occasional large values
    static std::vector<uint8_t> encodePForDelta(const std::vector<uint32_t>& values) {
        std::vector<uint8_t> encoded;
        
        if (values.empty()) {
            return encoded;
        }

        // Encode the count
        encodeVarInt(static_cast<uint32_t>(values.size()), encoded);

        if (values.size() == 1) {
            encodeVarInt(values[0], encoded);
            return encoded;
        }

        // Sort values
        std::vector<uint32_t> sorted_values = values;
        std::sort(sorted_values.begin(), sorted_values.end());

        // Use the minimum value as the base
        const uint32_t base = sorted_values[0];
        encodeVarInt(base, encoded);

        // Calculate deltas from base
        std::vector<uint32_t> deltas;
        deltas.reserve(sorted_values.size() - 1);
        
        for (size_t i = 1; i < sorted_values.size(); ++i) {
            deltas.push_back(sorted_values[i] - base);
        }

        if (deltas.empty()) {
            encoded.push_back(0); // bits_per_value = 0
            return encoded;
        }

        // Analyze deltas to find optimal bit width and exceptions
        // Use 90th percentile to determine normal bit width
        std::vector<uint32_t> sorted_deltas = deltas;
        std::sort(sorted_deltas.begin(), sorted_deltas.end());
        
        const size_t percentile_90_idx = static_cast<size_t>(sorted_deltas.size() * 0.9);
        const uint32_t threshold = sorted_deltas[percentile_90_idx];
        const uint8_t bits_per_value = threshold == 0 ? 0 : (32 - __builtin_clz(threshold));
        
        // Find exceptions (values that don't fit in bits_per_value)
        const uint32_t max_value = bits_per_value == 0 ? 0 : ((1U << bits_per_value) - 1);
        std::vector<uint32_t> exception_indices;
        std::vector<uint32_t> exception_values;
        
        for (size_t i = 0; i < deltas.size(); ++i) {
            if (deltas[i] > max_value) {
                exception_indices.push_back(static_cast<uint32_t>(i));
                exception_values.push_back(deltas[i]);
            }
        }

        // Encode bits per value
        encoded.push_back(bits_per_value);
        
        // Encode number of exceptions
        encodeVarInt(static_cast<uint32_t>(exception_indices.size()), encoded);
        
        // Encode exception indices and values
        for (size_t i = 0; i < exception_indices.size(); ++i) {
            encodeVarInt(exception_indices[i], encoded);
            encodeVarInt(exception_values[i], encoded);
        }

        if (bits_per_value == 0) {
            // All values fit in 0 bits (all same)
            return encoded;
        }

        // Bit-pack the regular values (clamped to max_value for exceptions)
        uint64_t buffer = 0;
        size_t bit_pos = 0;
        
        for (uint32_t delta : deltas) {
            const uint32_t clamped_value = std::min(delta, max_value);
            buffer |= (static_cast<uint64_t>(clamped_value) << bit_pos);
            bit_pos += bits_per_value;
            
            while (bit_pos >= 8) {
                encoded.push_back(static_cast<uint8_t>(buffer & 0xFF));
                buffer >>= 8;
                bit_pos -= 8;
            }
        }
        
        // Flush remaining bits
        if (bit_pos > 0) {
            encoded.push_back(static_cast<uint8_t>(buffer & 0xFF));
        }

        return encoded;
    }

    // Decode PForDelta encoded data
    static std::vector<uint32_t> decodePForDelta(const std::vector<uint8_t>& encoded) {
        std::vector<uint32_t> values;
        
        if (encoded.empty()) {
            return values;
        }

        size_t offset = 0;
        
        // Decode count
        const uint32_t count = decodeVarInt(encoded, offset);
        if (count == 0) {
            return values;
        }

        values.reserve(count);

        // Decode base value
        const uint32_t base = decodeVarInt(encoded, offset);
        values.push_back(base);

        if (count == 1) {
            return values;
        }

        // Decode bits per value
        if (offset >= encoded.size()) {
            throw std::runtime_error("Invalid PForDelta encoding: missing bits_per_value");
        }
        const uint8_t bits_per_value = encoded[offset++];
        
        // Decode number of exceptions
        const uint32_t num_exceptions = decodeVarInt(encoded, offset);
        
        // Decode exceptions
        std::unordered_map<uint32_t, uint32_t> exceptions;
        for (uint32_t i = 0; i < num_exceptions; ++i) {
            const uint32_t idx = decodeVarInt(encoded, offset);
            const uint32_t val = decodeVarInt(encoded, offset);
            exceptions[idx] = val;
        }

        if (bits_per_value == 0) {
            // All values are the same
            for (uint32_t i = 1; i < count; ++i) {
                const auto it = exceptions.find(i - 1);
                const uint32_t delta = (it != exceptions.end()) ? it->second : 0;
                values.push_back(base + delta);
            }
            return values;
        }

        // Unpack regular values
        std::vector<uint32_t> deltas;
        deltas.reserve(count - 1);
        
        uint64_t buffer = 0;
        size_t buffered_bits = 0;
        
        for (size_t i = 0; i < count - 1; ++i) {
            // Ensure we have enough bits in buffer
            while (buffered_bits < bits_per_value && offset < encoded.size()) {
                buffer |= (static_cast<uint64_t>(encoded[offset++]) << buffered_bits);
                buffered_bits += 8;
            }
            
            // Extract delta
            const uint64_t mask = (1ULL << bits_per_value) - 1;
            uint32_t delta = static_cast<uint32_t>(buffer & mask);
            
            // Replace with exception value if exists
            const auto it = exceptions.find(static_cast<uint32_t>(i));
            if (it != exceptions.end()) {
                delta = it->second;
            }
            
            deltas.push_back(delta);
            values.push_back(base + delta);
            
            buffer >>= bits_per_value;
            buffered_bits -= bits_per_value;
        }

        return values;
    }

    // Encode positions with delta encoding (for compatibility)
    static std::vector<uint8_t> encodeDeltaVarInt(const std::vector<uint32_t>& positions) {
        std::vector<uint8_t> encoded;
        if (positions.empty()) {
            return encoded;
        }

        std::vector<uint32_t> sorted_positions = positions;
        std::sort(sorted_positions.begin(), sorted_positions.end());

        // Encode first position
        encodeVarInt(sorted_positions[0], encoded);

        // Encode deltas
        for (size_t i = 1; i < sorted_positions.size(); ++i) {
            const uint32_t delta = sorted_positions[i] - sorted_positions[i - 1];
            encodeVarInt(delta, encoded);
        }

        return encoded;
    }

    // Decode delta-encoded positions
    static std::vector<uint32_t> decodeDeltaVarInt(const std::vector<uint8_t>& encoded) {
        std::vector<uint32_t> positions;
        
        if (encoded.empty()) {
            return positions;
        }

        size_t offset = 0;
        uint32_t current_position = decodeVarInt(encoded, offset);
        positions.push_back(current_position);

        while (offset < encoded.size()) {
            const uint32_t delta = decodeVarInt(encoded, offset);
            current_position += delta;
            positions.push_back(current_position);
        }

        return positions;
    }
};
