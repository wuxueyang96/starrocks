#include <iostream>
#include <vector>
#include <thread>
#include <random>
#include <cassert>
#include <chrono>

#include "memory_pool.h"
#include <roaring/roaring.hh>

void test_basic_allocation() {
    std::cout << "=== Test Basic Allocation ===" << std::endl;
    
    LayeredMemoryPool pool;
    MemoryPoolConfig config(16 * 1024 * 1024); // 16 MB
    assert(pool.initialize(config));
    
    // 测试小块分配
    void* p1 = pool.allocate(32);
    assert(p1 != nullptr);
    std::cout << "  Allocated 32 bytes: " << p1 << std::endl;
    
    void* p2 = pool.allocate(64);
    assert(p2 != nullptr);
    std::cout << "  Allocated 64 bytes: " << p2 << std::endl;
    
    void* p3 = pool.allocate(256);
    assert(p3 != nullptr);
    std::cout << "  Allocated 256 bytes: " << p3 << std::endl;
    
    // 测试大块分配
    void* p4 = pool.allocate(8192);
    assert(p4 != nullptr);
    std::cout << "  Allocated 8192 bytes: " << p4 << std::endl;
    
    // 释放
    pool.deallocate(p1);
    pool.deallocate(p2);
    pool.deallocate(p3);
    pool.deallocate(p4);
    
    auto stats = pool.get_stats();
    std::cout << "  Allocations: " << stats.allocation_count << std::endl;
    std::cout << "  Frees: " << stats.free_count << std::endl;
    std::cout << "  Used size: " << stats.used_size << std::endl;
    
    pool.destroy();
    std::cout << "  PASSED!" << std::endl;
}

void test_reallocation() {
    std::cout << "\n=== Test Reallocation ===" << std::endl;
    
    LayeredMemoryPool pool;
    MemoryPoolConfig config(8 * 1024 * 1024); // 8 MB
    assert(pool.initialize(config));
    
    // 分配小块
    void* p = pool.allocate(32);
    assert(p != nullptr);
    std::memset(p, 0xAB, 32);
    
    // 重新分配为更大
    void* p2 = pool.reallocate(p, 128);
    assert(p2 != nullptr);
    
    // 验证数据保留
    uint8_t* bytes = static_cast<uint8_t*>(p2);
    for (int i = 0; i < 32; ++i) {
        assert(bytes[i] == 0xAB);
    }
    
    pool.deallocate(p2);
    pool.destroy();
    std::cout << "  PASSED!" << std::endl;
}

void test_calloc() {
    std::cout << "\n=== Test Calloc ===" << std::endl;
    
    LayeredMemoryPool pool;
    MemoryPoolConfig config(8 * 1024 * 1024);
    assert(pool.initialize(config));
    
    void* p = pool.callocate(10, 100);
    assert(p != nullptr);
    
    // 验证清零
    uint8_t* bytes = static_cast<uint8_t*>(p);
    for (int i = 0; i < 1000; ++i) {
        assert(bytes[i] == 0);
    }
    
    pool.deallocate(p);
    pool.destroy();
    std::cout << "  PASSED!" << std::endl;
}

void test_aligned_allocation() {
    std::cout << "\n=== Test Aligned Allocation ===" << std::endl;
    
    LayeredMemoryPool pool;
    MemoryPoolConfig config(8 * 1024 * 1024);
    assert(pool.initialize(config));
    
    // 测试不同对齐
    for (size_t alignment : {16, 32, 64, 128, 256}) {
        void* p = pool.allocate_aligned(alignment, 100);
        assert(p != nullptr);
        
        uintptr_t addr = reinterpret_cast<uintptr_t>(p);
        assert((addr % alignment) == 0);
        std::cout << "  Alignment " << alignment << ": address " << p << " OK" << std::endl;
        
        pool.deallocate_aligned(p);
    }
    
    pool.destroy();
    std::cout << "  PASSED!" << std::endl;
}

void test_out_of_memory() {
    std::cout << "\n=== Test Out of Memory ===" << std::endl;
    
    LayeredMemoryPool pool;
    MemoryPoolConfig config(1024 * 1024); // 只有 1MB
    assert(pool.initialize(config));
    
    // 尝试分配超过池大小的内存
    void* p = pool.allocate(2 * 1024 * 1024);
    assert(p == nullptr);
    std::cout << "  Large allocation failed as expected" << std::endl;
    
    // 分配直到耗尽
    std::vector<void*> ptrs;
    int count = 0;
    while (true) {
        void* ptr = pool.allocate(4096);
        if (!ptr) break;
        ptrs.push_back(ptr);
        ++count;
    }
    std::cout << "  Allocated " << count << " blocks of 4096 bytes before exhaustion" << std::endl;
    
    auto stats = pool.get_stats();
    std::cout << "  Failed allocations: " << stats.failed_allocations << std::endl;
    
    // 释放所有
    for (void* ptr : ptrs) {
        pool.deallocate(ptr);
    }
    
    pool.destroy();
    std::cout << "  PASSED!" << std::endl;
}

void test_multithreaded() {
    std::cout << "\n=== Test Multithreaded Allocation ===" << std::endl;
    
    LayeredMemoryPool pool;
    MemoryPoolConfig config(64 * 1024 * 1024); // 64 MB
    assert(pool.initialize(config));
    
    constexpr int NUM_THREADS = 4;
    constexpr int ITERATIONS = 1000;
    
    auto worker = [&pool](int thread_id) {
        std::mt19937 rng(thread_id);
        std::uniform_int_distribution<size_t> size_dist(16, 8192);
        
        std::vector<void*> ptrs;
        ptrs.reserve(ITERATIONS);
        
        for (int i = 0; i < ITERATIONS; ++i) {
            size_t size = size_dist(rng);
            void* p = pool.allocate(size);
            if (p) {
                ptrs.push_back(p);
            }
            
            // 随机释放一些
            if (!ptrs.empty() && (rng() % 3 == 0)) {
                size_t idx = rng() % ptrs.size();
                pool.deallocate(ptrs[idx]);
                ptrs.erase(ptrs.begin() + idx);
            }
        }
        
        // 释放剩余
        for (void* p : ptrs) {
            pool.deallocate(p);
        }
    };
    
    std::vector<std::thread> threads;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    auto stats = pool.get_stats();
    std::cout << "  Time: " << duration.count() << " ms" << std::endl;
    std::cout << "  Total allocations: " << stats.allocation_count << std::endl;
    std::cout << "  Total frees: " << stats.free_count << std::endl;
    std::cout << "  Peak used: " << stats.peak_used_size << " bytes" << std::endl;
    
    pool.destroy();
    std::cout << "  PASSED!" << std::endl;
}

void test_roaring_integration() {
    std::cout << "\n=== Test Roaring Integration ===" << std::endl;
    
    // 初始化全局内存池
    auto& pool = LayeredMemoryPool::instance();
    MemoryPoolConfig config(32 * 1024 * 1024); // 32 MB
    if (!pool.is_initialized()) {
        assert(pool.initialize(config));
    }
    
    // 注册到 roaring
    pool.register_with_roaring();
    std::cout << "  Registered memory pool with roaring" << std::endl;
    
    auto stats_before = pool.get_stats();
    
    // 创建一些 roaring bitmap
    {
        roaring::Roaring r1;
        for (uint32_t i = 0; i < 10000; i += 3) {
            r1.add(i);
        }
        std::cout << "  Created roaring bitmap with " << r1.cardinality() << " elements" << std::endl;
        
        roaring::Roaring r2;
        for (uint32_t i = 0; i < 10000; i += 5) {
            r2.add(i);
        }
        
        roaring::Roaring r3 = r1 & r2;
        std::cout << "  Intersection has " << r3.cardinality() << " elements" << std::endl;
        
        roaring::Roaring r4 = r1 | r2;
        std::cout << "  Union has " << r4.cardinality() << " elements" << std::endl;
    }
    
    auto stats_after = pool.get_stats();
    std::cout << "  Pool allocations: " << stats_after.allocation_count - stats_before.allocation_count << std::endl;
    std::cout << "  Peak memory used: " << stats_after.peak_used_size << " bytes" << std::endl;
    
    std::cout << "  PASSED!" << std::endl;
}

void print_stats(const LayeredMemoryPool::Stats& stats) {
    std::cout << "\n=== Memory Pool Statistics ===" << std::endl;
    std::cout << "  Total size: " << stats.total_size / (1024 * 1024) << " MB" << std::endl;
    std::cout << "  Used size: " << stats.used_size << " bytes" << std::endl;
    std::cout << "  Peak used: " << stats.peak_used_size << " bytes" << std::endl;
    std::cout << "  Allocations: " << stats.allocation_count << std::endl;
    std::cout << "  Frees: " << stats.free_count << std::endl;
    std::cout << "  Failed allocations: " << stats.failed_allocations << std::endl;
    
    std::cout << "  Layer allocations:" << std::endl;
    const char* layer_names[] = {"32B", "64B", "128B", "256B", "512B", "1KB", "2KB", "4KB"};
    for (size_t i = 0; i < stats.layer_allocations.size(); ++i) {
        if (stats.layer_allocations[i] > 0) {
            std::cout << "    " << layer_names[i] << ": " << stats.layer_allocations[i] << std::endl;
        }
    }
    std::cout << "  Large allocations: " << stats.large_allocations << std::endl;
}

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "  Layered Memory Pool Test Suite" << std::endl;
    std::cout << "====================================" << std::endl;
    
    test_basic_allocation();
    test_reallocation();
    test_calloc();
    test_aligned_allocation();
    test_out_of_memory();
    test_multithreaded();
    test_roaring_integration();
    
    // 打印全局池统计
    print_stats(LayeredMemoryPool::instance().get_stats());
    
    std::cout << "\n====================================" << std::endl;
    std::cout << "  All tests passed!" << std::endl;
    std::cout << "====================================" << std::endl;
    
    return 0;
}
