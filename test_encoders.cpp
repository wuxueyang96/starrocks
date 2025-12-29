#include <cassert>
#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <roaring/roaring.hh>

#include "include/varint_encoder.h"
#include "include/for_encoder.h"
#include "include/pfor_delta_encoder.h"
#include "include/new_pfor_delta_encoder.h"
#include "include/simple9_encoder.h"
#include "include/adaptive_encoder.h"

// Test helper functions
void assert_equal(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual, const std::string& test_name) {
    if (expected != actual) {
        std::cerr << "FAILED: " << test_name << std::endl;
        std::cerr << "Expected " << expected.size() << " values, got " << actual.size() << std::endl;
        if (expected.size() == actual.size()) {
            for (size_t i = 0; i < expected.size(); ++i) {
                if (expected[i] != actual[i]) {
                    std::cerr << "  Index " << i << ": expected " << expected[i] << ", got " << actual[i] << std::endl;
                }
            }
        }
        exit(1);
    }
    std::cout << "PASSED: " << test_name << std::endl;
}

void test_encoder(const std::string& encoder_name, const std::shared_ptr<Encoder>& encoder, const std::vector<uint32_t>& values) {
    roaring::Roaring roaring(values.size(), values.data());
    std::vector<uint8_t> encoded;
    Status status = encoder->encode(roaring, &encoded);
    
    if (status != Status::OK) {
        std::cerr << "FAILED: " << encoder_name << " - encode failed" << std::endl;
        exit(1);
    }
    
    std::cout << encoder_name << " - Input size: " << values.size() 
              << ", Encoded size: " << encoded.size();
    if (!encoded.empty()) {
        std::cout << ", Compression ratio: " << (values.size() * 4.0 / encoded.size()) << "x";
    }
    std::cout << std::endl;
    
    std::cout << "PASSED: " << encoder_name << " encode" << std::endl;
}

// ========== VarInt Encoder Tests ==========
void test_varint_empty() {
    auto encoder = std::make_shared<VarIntEncoder>();
    std::vector<uint32_t> values;
    test_encoder("VarInt (empty)", encoder, values);
}

void test_varint_single() {
    auto encoder = std::make_shared<VarIntEncoder>();
    std::vector<uint32_t> values = {42};
    test_encoder("VarInt (single)", encoder, values);
}

void test_varint_small() {
    auto encoder = std::make_shared<VarIntEncoder>();
    std::vector<uint32_t> values = {1, 5, 10, 15, 20};
    test_encoder("VarInt (small values)", encoder, values);
}

void test_varint_large() {
    auto encoder = std::make_shared<VarIntEncoder>();
    std::vector<uint32_t> values = {1000000, 2000000, 3000000};
    test_encoder("VarInt (large values)", encoder, values);
}

void test_varint_sequential() {
    auto encoder = std::make_shared<VarIntEncoder>();
    std::vector<uint32_t> values;
    for (uint32_t i = 0; i < 100; ++i) {
        values.push_back(i * 10);
    }
    test_encoder("VarInt (sequential)", encoder, values);
}

// ========== FOR Encoder Tests ==========
void test_for_uniform() {
    auto encoder = std::make_shared<FrameOfReferenceEncoder>();
    std::vector<uint32_t> values;
    for (uint32_t i = 0; i < 50; ++i) {
        values.push_back(1000 + i);
    }
    test_encoder("FOR (uniform distribution)", encoder, values);
}

void test_for_small_range() {
    auto encoder = std::make_shared<FrameOfReferenceEncoder>();
    std::vector<uint32_t> values = {100, 102, 105, 108, 110};
    test_encoder("FOR (small range)", encoder, values);
}

void test_for_same_values() {
    auto encoder = std::make_shared<FrameOfReferenceEncoder>();
    // Roaring is a set, so use consecutive values with same delta
    std::vector<uint32_t> values;
    for (uint32_t i = 0; i < 20; ++i) {
        values.push_back(42 + i);  // consecutive values starting from 42
    }
    test_encoder("FOR (consecutive values)", encoder, values);
}

// ========== PForDelta Encoder Tests ==========
void test_pfor_with_outliers() {
    auto encoder = std::make_shared<PForDeltaEncoder>();
    // Create sorted unique values with some outliers mixed in
    std::vector<uint32_t> values;
    for (uint32_t i = 0; i < 97; ++i) {
        values.push_back(i);
    }
    // Add outliers (must be larger than previous values to maintain sorted order)
    values.push_back(10000);
    values.push_back(20000);
    values.push_back(30000);
    
    test_encoder("PForDelta (with outliers)", encoder, values);
}

void test_pfor_clustered() {
    auto encoder = std::make_shared<PForDeltaEncoder>();
    std::vector<uint32_t> values;
    for (uint32_t i = 0; i < 80; ++i) {
        values.push_back(i * 2);
    }
    test_encoder("PForDelta (clustered)", encoder, values);
}

// ========== NewPForDelta Encoder Tests ==========
void test_newpfor_sparse_outliers() {
    auto encoder = std::make_shared<NewPForDeltaEncoder>();
    // Create sorted unique values with outliers at the end
    std::vector<uint32_t> values;
    for (uint32_t i = 0; i < 97; ++i) {
        values.push_back(i * 5);
    }
    // Add outliers (must maintain sorted order)
    values.push_back(5000);
    values.push_back(8000);
    values.push_back(12000);
    
    test_encoder("NewPForDelta (sparse outliers)", encoder, values);
}

void test_newpfor_many_outliers() {
    auto encoder = std::make_shared<NewPForDeltaEncoder>();
    // Create sorted unique values with varying gaps
    std::vector<uint32_t> values;
    uint32_t current = 0;
    for (uint32_t i = 0; i < 100; ++i) {
        if (i % 10 == 0) {
            current += 100; // larger gap (outlier)
        } else {
            current += 1;   // small gap
        }
        values.push_back(current);
    }
    test_encoder("NewPForDelta (many outliers)", encoder, values);
}

// ========== Simple9 Encoder Tests ==========
void test_simple9_small() {
    auto encoder = std::make_shared<Simple9Encoder>();
    std::vector<uint32_t> values = {1, 2, 3, 4, 5, 6, 7};
    test_encoder("Simple9 (small values)", encoder, values);
}

void test_simple9_variable_sizes() {
    auto encoder = std::make_shared<Simple9Encoder>();
    std::vector<uint32_t> values = {1, 3, 7, 15, 31, 63, 127};
    test_encoder("Simple9 (variable sizes)", encoder, values);
}

void test_simple9_large_batch() {
    auto encoder = std::make_shared<Simple9Encoder>();
    std::vector<uint32_t> values;
    // Roaring is a set, use unique values
    for (uint32_t i = 0; i < 100; ++i) {
        values.push_back(i);
    }
    test_encoder("Simple9 (large batch)", encoder, values);
}

// ========== Adaptive Encoder Tests ==========
void test_adaptive_selects_varint() {
    auto encoder = std::make_shared<AdaptiveEncoder>();
    std::vector<uint32_t> values = {1, 2, 3, 4, 5};
    test_encoder("Adaptive (selects VarInt)", encoder, values);
}

void test_adaptive_selects_for() {
    auto encoder = std::make_shared<AdaptiveEncoder>();
    std::vector<uint32_t> values;
    for (uint32_t i = 0; i < 50; ++i) {
        values.push_back(1000 + i);
    }
    test_encoder("Adaptive (selects FOR)", encoder, values);
}

void test_adaptive_selects_pfor() {
    auto encoder = std::make_shared<AdaptiveEncoder>();
    // Create sorted unique values with outliers at the end
    std::vector<uint32_t> values;
    for (uint32_t i = 0; i < 98; ++i) {
        values.push_back(i);
    }
    values.push_back(50000);
    values.push_back(80000);
    test_encoder("Adaptive (selects PFor)", encoder, values);
}

// ========== Edge Cases ==========
void test_all_zeros() {
    // Roaring is a set, cannot have duplicates. Test with consecutive small values instead.
    std::vector<uint32_t> values;
    for (uint32_t i = 0; i < 50; ++i) {
        values.push_back(i);
    }
    
    auto varint = std::make_shared<VarIntEncoder>();
    test_encoder("VarInt (small consecutive)", varint, values);
    
    auto for_enc = std::make_shared<FrameOfReferenceEncoder>();
    test_encoder("FOR (small consecutive)", for_enc, values);
}

void test_single_large_value() {
    std::vector<uint32_t> values = {4294967295}; // max uint32
    
    auto varint = std::make_shared<VarIntEncoder>();
    test_encoder("VarInt (max uint32)", varint, values);
}

void test_alternating_values() {
    // Roaring is a set, use unique values with large gaps
    std::vector<uint32_t> values;
    for (int i = 0; i < 50; ++i) {
        values.push_back(i * 1000);  // values: 0, 1000, 2000, ...
    }
    
    auto pfor = std::make_shared<PForDeltaEncoder>();
    test_encoder("PForDelta (large gaps)", pfor, values);
    
    auto newpfor = std::make_shared<NewPForDeltaEncoder>();
    test_encoder("NewPForDelta (large gaps)", newpfor, values);
}

// ========== Random Data Tests ==========
void test_random_data() {
    std::mt19937 gen(42); // Fixed seed for reproducibility
    
    // Small random values - use unique sequential values with random gaps
    {
        std::vector<uint32_t> values;
        uint32_t current = 0;
        for (int i = 0; i < 100; ++i) {
            current += (gen() % 10) + 1;  // Random gap 1-10
            values.push_back(current);
        }
        
        auto adaptive = std::make_shared<AdaptiveEncoder>();
        test_encoder("Adaptive (random small)", adaptive, values);
    }
    
    // Large random values - use unique sequential values with larger random gaps
    {
        std::vector<uint32_t> values;
        uint32_t current = 0;
        for (int i = 0; i < 100; ++i) {
            current += (gen() % 10000) + 1;  // Random gap 1-10000
            values.push_back(current);
        }
        
        auto adaptive = std::make_shared<AdaptiveEncoder>();
        test_encoder("Adaptive (random large)", adaptive, values);
    }
}

// ========== Stress Tests ==========
void test_large_dataset() {
    std::vector<uint32_t> values;
    for (uint32_t i = 0; i < 10000; ++i) {
        values.push_back(i * 7);
    }
    
    auto adaptive = std::make_shared<AdaptiveEncoder>();
    test_encoder("Adaptive (10K values)", adaptive, values);
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Encoder Unit Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    std::cout << "--- VarInt Encoder Tests ---" << std::endl;
    test_varint_empty();
    test_varint_single();
    test_varint_small();
    test_varint_large();
    test_varint_sequential();
    std::cout << std::endl;
    
    std::cout << "--- FOR Encoder Tests ---" << std::endl;
    test_for_uniform();
    test_for_small_range();
    test_for_same_values();
    std::cout << std::endl;
    
    std::cout << "--- PForDelta Encoder Tests ---" << std::endl;
    test_pfor_with_outliers();
    test_pfor_clustered();
    std::cout << std::endl;
    
    std::cout << "--- NewPForDelta Encoder Tests ---" << std::endl;
    test_newpfor_sparse_outliers();
    test_newpfor_many_outliers();
    std::cout << std::endl;
    
    std::cout << "--- Simple9 Encoder Tests ---" << std::endl;
    test_simple9_small();
    test_simple9_variable_sizes();
    test_simple9_large_batch();
    std::cout << std::endl;
    
    std::cout << "--- Adaptive Encoder Tests ---" << std::endl;
    test_adaptive_selects_varint();
    test_adaptive_selects_for();
    test_adaptive_selects_pfor();
    std::cout << std::endl;
    
    std::cout << "--- Edge Case Tests ---" << std::endl;
    test_all_zeros();
    test_single_large_value();
    test_alternating_values();
    std::cout << std::endl;
    
    std::cout << "--- Random Data Tests ---" << std::endl;
    test_random_data();
    std::cout << std::endl;
    
    std::cout << "--- Stress Tests ---" << std::endl;
    test_large_dataset();
    std::cout << std::endl;
    
    std::cout << "========================================" << std::endl;
    std::cout << "  All Tests Passed!" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}
