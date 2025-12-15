#include <cassert>
#include <iostream>
#include <vector>
#include <random>
#include <algorithm>

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
    std::vector<uint8_t> encoded = encoder->encode(values, 0, values.size());
    std::vector<uint32_t> decoded = encoder->decode(encoded.data(), encoded.size());
    
    std::cout << encoder_name << " - Input size: " << values.size() 
              << ", Encoded size: " << encoded.size() 
              << ", Compression ratio: " << (values.size() * 4.0 / encoded.size()) << "x" << std::endl;
    
    assert_equal(values, decoded, encoder_name + " encode/decode");
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
    std::vector<uint32_t> values(20, 42);
    test_encoder("FOR (same values)", encoder, values);
}

// ========== PForDelta Encoder Tests ==========
void test_pfor_with_outliers() {
    auto encoder = std::make_shared<PForDeltaEncoder>();
    std::vector<uint32_t> values;
    for (uint32_t i = 0; i < 100; ++i) {
        values.push_back(i);
    }
    // Add some outliers
    values[10] = 10000;
    values[50] = 20000;
    values[90] = 30000;
    
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
    std::vector<uint32_t> values;
    for (uint32_t i = 0; i < 100; ++i) {
        values.push_back(i * 5);
    }
    // Add sparse outliers
    values[5] = 5000;
    values[25] = 8000;
    values[75] = 12000;
    
    test_encoder("NewPForDelta (sparse outliers)", encoder, values);
}

void test_newpfor_many_outliers() {
    auto encoder = std::make_shared<NewPForDeltaEncoder>();
    std::vector<uint32_t> values;
    for (uint32_t i = 0; i < 100; ++i) {
        if (i % 10 == 0) {
            values.push_back(i * 100); // outlier
        } else {
            values.push_back(i);
        }
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
    for (uint32_t i = 0; i < 100; ++i) {
        values.push_back(i % 128);
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
    std::vector<uint32_t> values;
    for (uint32_t i = 0; i < 100; ++i) {
        values.push_back(i);
    }
    values[20] = 50000;
    values[60] = 80000;
    test_encoder("Adaptive (selects PFor)", encoder, values);
}

// ========== Edge Cases ==========
void test_all_zeros() {
    std::vector<uint32_t> values(50, 0);
    
    auto varint = std::make_shared<VarIntEncoder>();
    test_encoder("VarInt (all zeros)", varint, values);
    
    auto for_enc = std::make_shared<FrameOfReferenceEncoder>();
    test_encoder("FOR (all zeros)", for_enc, values);
}

void test_single_large_value() {
    std::vector<uint32_t> values = {4294967295}; // max uint32
    
    auto varint = std::make_shared<VarIntEncoder>();
    test_encoder("VarInt (max uint32)", varint, values);
}

void test_alternating_values() {
    std::vector<uint32_t> values;
    for (int i = 0; i < 50; ++i) {
        values.push_back(i % 2 == 0 ? 10 : 10000);
    }
    
    auto pfor = std::make_shared<PForDeltaEncoder>();
    test_encoder("PForDelta (alternating)", pfor, values);
    
    auto newpfor = std::make_shared<NewPForDeltaEncoder>();
    test_encoder("NewPForDelta (alternating)", newpfor, values);
}

// ========== Random Data Tests ==========
void test_random_data() {
    std::random_device rd;
    std::mt19937 gen(42); // Fixed seed for reproducibility
    
    // Small random values
    {
        std::uniform_int_distribution<uint32_t> dist(0, 255);
        std::vector<uint32_t> values;
        for (int i = 0; i < 100; ++i) {
            values.push_back(dist(gen));
        }
        std::sort(values.begin(), values.end());
        
        auto adaptive = std::make_shared<AdaptiveEncoder>();
        test_encoder("Adaptive (random small)", adaptive, values);
    }
    
    // Large random values
    {
        std::uniform_int_distribution<uint32_t> dist(0, 1000000);
        std::vector<uint32_t> values;
        for (int i = 0; i < 100; ++i) {
            values.push_back(dist(gen));
        }
        std::sort(values.begin(), values.end());
        
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
