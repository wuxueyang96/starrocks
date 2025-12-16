#include "pfor_delta_encoder.h"
#include "varint_encoder.h"
#include "simple9_encoder.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_map>

std::vector<uint8_t> PForDeltaEncoder::encode(const std::vector<uint32_t>& values, size_t start, size_t end) {
    std::vector<uint8_t> encoded;
    
    if (start >= end) {
        return encoded;
    }
    
    const size_t total_count = end - start;
    
    // Encode total count
    VarIntEncoder::encodeValue(static_cast<uint32_t>(total_count), encoded);
    
    // Process in fixed-size blocks
    size_t pos = start;
    while (pos < end) {
        const size_t block_end = std::min(pos + BLOCK_SIZE, end);
        const std::vector<uint8_t> block_data = encodeBlock(values, pos, block_end);
        
        // Just append block data directly - no need to store size since we know BLOCK_SIZE
        encoded.insert(encoded.end(), block_data.begin(), block_data.end());
        
        pos = block_end;
    }
    
    return encoded;
}

std::vector<uint32_t> PForDeltaEncoder::decode(const uint8_t* encoded, size_t size) {
    std::vector<uint32_t> values;
    
    if (size == 0) {
        return values;
    }
    
    size_t offset = 0;
    const std::vector<uint8_t> encoded_vec(encoded, encoded + size);
    
    // Decode total count
    const uint32_t total_count = VarIntEncoder::decodeValue(encoded_vec, offset);
    values.reserve(total_count);
    
    // Decode blocks
    while (values.size() < total_count && offset < size) {
        offset = decodeBlock(encoded_vec, offset, values);
    }
    
    return values;
}

std::vector<uint8_t> PForDeltaEncoder::encodeBlock(const std::vector<uint32_t>& values, size_t start, size_t end) {
    std::vector<uint8_t> encoded;
    
    if (start >= end) {
        return encoded;
    }
    
    const size_t block_size = end - start;
    
    if (block_size == 1) {
        VarIntEncoder::encodeValue(values[start], encoded);
        return encoded;
    }
    
    // Find min value as base for this block
    uint32_t base = values[start];
    for (size_t i = start + 1; i < end; ++i) {
        base = std::min(base, values[i]);
    }
    VarIntEncoder::encodeValue(base, encoded);
    
    // Calculate deltas from base
    std::vector<uint32_t> deltas;
    deltas.reserve(block_size);
    for (size_t i = start; i < end; ++i) {
        deltas.push_back(values[i] - base);
    }
    
    // Find 90th percentile for bit width
    std::vector<uint32_t> sorted_deltas = deltas;
    std::sort(sorted_deltas.begin(), sorted_deltas.end());
    
    const size_t percentile_90_idx = std::min(static_cast<size_t>(sorted_deltas.size() * 0.9), sorted_deltas.size() - 1);
    const uint32_t threshold = sorted_deltas[percentile_90_idx];
    const uint8_t bits_per_value = threshold == 0 ? 0 : (32 - __builtin_clz(threshold));
    
    // Find exceptions
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
    VarIntEncoder::encodeValue(static_cast<uint32_t>(exception_indices.size()), encoded);
    
    // Encode exception indices using VarInt
    for (uint32_t idx : exception_indices) {
        VarIntEncoder::encodeValue(idx, encoded);
    }
    
    // Encode exception values using Simple9
    if (!exception_values.empty()) {
        size_t exc_idx = 0;
        while (exc_idx < exception_values.size()) {
            const size_t encoded_count = Simple9Encoder::encodeBatch(exception_values, exc_idx, encoded);
            exc_idx += encoded_count;
        }
    }
    
    if (bits_per_value == 0) {
        return encoded;
    }
    
    // Bit-pack the regular values
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

size_t PForDeltaEncoder::decodeBlock(const std::vector<uint8_t>& encoded, size_t offset, std::vector<uint32_t>& output) {
    const size_t initial_offset = offset;
    
    if (offset >= encoded.size()) {
        return offset;
    }
    
    // Decode base value
    const uint32_t base = VarIntEncoder::decodeValue(encoded, offset);
    
    // Check if single value case
    if (offset >= encoded.size()) {
        output.push_back(base);
        return offset;
    }
    
    // Decode bits per value
    const uint8_t bits_per_value = encoded[offset++];
    
    // Decode number of exceptions
    const uint32_t num_exceptions = VarIntEncoder::decodeValue(encoded, offset);
    
    // Decode exception indices
    std::vector<uint32_t> exception_indices;
    exception_indices.reserve(num_exceptions);
    for (uint32_t i = 0; i < num_exceptions; ++i) {
        exception_indices.push_back(VarIntEncoder::decodeValue(encoded, offset));
    }
    
    // Decode exception values using Simple9
    std::vector<uint32_t> exception_values;
    exception_values.reserve(num_exceptions);
    
    while (exception_values.size() < num_exceptions && offset < encoded.size()) {
        if (offset + 4 <= encoded.size()) {
            const uint32_t word = static_cast<uint32_t>(encoded[offset]) |
                                  (static_cast<uint32_t>(encoded[offset + 1]) << 8) |
                                  (static_cast<uint32_t>(encoded[offset + 2]) << 16) |
                                  (static_cast<uint32_t>(encoded[offset + 3]) << 24);
            offset += 4;
            
            const uint8_t selector = static_cast<uint8_t>(word >> 28);
            
            if (selector >= 9) {
                throw std::runtime_error("Invalid Simple9 selector in PForDelta");
            }
            
            const auto& mode = Simple9Encoder::MODES[selector];
            const uint32_t mask = (1U << mode.bits) - 1;
            
            for (uint8_t i = 0; i < mode.count && exception_values.size() < num_exceptions; ++i) {
                const uint32_t value = (word >> (i * mode.bits)) & mask;
                exception_values.push_back(value);
            }
        } else {
            break;
        }
    }
    
    // Build exception map
    std::unordered_map<uint32_t, uint32_t> exceptions;
    for (size_t i = 0; i < exception_indices.size(); ++i) {
        exceptions[exception_indices[i]] = exception_values[i];
    }
    
    if (bits_per_value == 0) {
        // All values are the same - determine count from exceptions or assume BLOCK_SIZE
        uint32_t count = BLOCK_SIZE;
        if (!exception_indices.empty()) {
            count = *std::max_element(exception_indices.begin(), exception_indices.end()) + 1;
        }
        for (uint32_t i = 0; i < count; ++i) {
            const auto it = exceptions.find(i);
            const uint32_t delta = (it != exceptions.end()) ? it->second : 0;
            output.push_back(base + delta);
        }
        return offset;
    }
    
    // For fixed-size blocks, decode exactly BLOCK_SIZE values (or until end of data)
    // Calculate the maximum we can decode from available data
    const size_t available_bytes = encoded.size() - offset;
    const size_t available_bits = available_bytes * 8;
    const size_t max_decodable = available_bits / bits_per_value;
    const size_t count = std::min(static_cast<size_t>(BLOCK_SIZE), max_decodable);
    
    // Unpack regular values
    uint64_t buffer = 0;
    size_t buffered_bits = 0;
    
    for (size_t i = 0; i < count && offset < encoded.size(); ++i) {
        // Ensure we have enough bits in buffer
        while (buffered_bits < bits_per_value && offset < encoded.size()) {
            buffer |= (static_cast<uint64_t>(encoded[offset++]) << buffered_bits);
            buffered_bits += 8;
        }
        
        if (buffered_bits < bits_per_value) {
            break;
        }
        
        // Extract delta
        const uint64_t mask = (1ULL << bits_per_value) - 1;
        uint32_t delta = static_cast<uint32_t>(buffer & mask);
        
        // Replace with exception value if exists
        const auto it = exceptions.find(static_cast<uint32_t>(i));
        if (it != exceptions.end()) {
            delta = it->second;
        }
        
        output.push_back(base + delta);
        
        buffer >>= bits_per_value;
        buffered_bits -= bits_per_value;
    }
    
    return offset;
}
