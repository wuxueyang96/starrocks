#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

#include "config.h"

/**
 * DeferOp - RAII 延迟执行机制（零分配 / 零类型擦除版本）
 * 在作用域结束时自动执行指定的操作
 *
 * 说明：
 * - 旧版本使用 std::function 可能触发堆分配与类型擦除开销
 * - 这里改为模板，直接存储 functor，通常可被编译器内联优化
 */
template <typename F>
class DeferOp {
public:
    explicit DeferOp(F&& func) noexcept(std::is_nothrow_constructible_v<F, F&&>) : func_(std::forward<F>(func)) {}

    ~DeferOp() noexcept(noexcept(std::declval<F&>()())) { func_(); }

    // 禁止拷贝和移动（保持原语义）
    DeferOp(const DeferOp&) = delete;
    DeferOp& operator=(const DeferOp&) = delete;
    DeferOp(DeferOp&&) = delete;
    DeferOp& operator=(DeferOp&&) = delete;

private:
    F func_;
};

extern "C" {
#include <roaring/memory.h>
}

/**
 * 分层内存池 (Layered Memory Pool)
 *
 * 设计特点：
 * - 总大小可配置，不支持扩容
 * - 内存不足时申请失败（返回 nullptr）
 * - 分层设计：小块使用固定大小块池，大块使用自由列表
 * - 线程安全
 *
 * 层级设计：
 * - Layer 0: 8 字节块
 * - Layer 1: 16 字节块
 * - Layer 2: 32 字节块
 * - Layer 3: 64 字节块
 * - Layer 4: 128 字节块
 * - Layer 5: 256 字节块
 * - Layer 6: 512 字节块
 * - Layer 7: 1024 字节块
 * - 大块区域: > 1024 字节，使用首次适应算法
 */
class LayeredMemoryPool {
public:
    // 层级配置
    static constexpr size_t NUM_LAYERS = 8;
    static constexpr size_t MIN_BLOCK_SIZE = 8; // 最小 8 字节，刚好存储一个指针
    static constexpr size_t MAX_SMALL_BLOCK_SIZE = 4096;

    // 每层的块大小（最小 8 字节，确保能存储 FreeBlock::next 指针）
    static constexpr std::array<size_t, NUM_LAYERS> BLOCK_SIZES = {8, 16, 32, 64, 128, 256, 512, 1024};

    /**
     * 内存池统计信息
     */
    struct Stats {
        size_t total_size;
        size_t used_size;
        size_t peak_used_size;
        size_t allocation_count;
        size_t free_count;
        size_t failed_allocations;
        std::array<size_t, NUM_LAYERS> layer_allocations;
        size_t large_allocations;
    };

public:
    // 空闲块链表节点（在空闲时使用内存块本身存储，使用时被用户数据覆盖）
    struct FreeBlock {
        FreeBlock* next; // 8 字节指针，因此最小块必须 >= 8 字节
    };

private:
    // 大块分配的头部信息
    struct LargeBlockHeader {
        size_t size; // 分配的大小（含头部）
        bool in_use; // 是否在使用中
        LargeBlockHeader* prev;
        LargeBlockHeader* next;
    };

    // 对齐分配的元数据
    struct AlignedMetadata {
        void* original_ptr; // 原始分配的指针
        size_t size;        // 原始分配的大小
    };

    MemoryPoolConfig config_;

    // 原始内存块
    uint8_t* memory_;

    // 小块区域
    uint8_t* small_block_start_;
    size_t small_block_size_;
    // 每层在小块区域内的实际地址范围（由 init_small_blocks() 真实计算）
    std::array<uint8_t*, NUM_LAYERS> small_layer_starts_{};
    std::array<uint8_t*, NUM_LAYERS> small_layer_ends_{};

    // 每层的空闲链表头（空闲时在内存块本身存储 FreeBlock）
    std::array<FreeBlock*, NUM_LAYERS> free_lists_;
    std::array<std::mutex, NUM_LAYERS> layer_mutexes_;

    // 大块区域
    uint8_t* large_block_start_;
    size_t large_block_size_;
    LargeBlockHeader* large_block_head_;
    std::mutex large_block_mutex_;

    // 统计信息
    std::atomic<size_t> used_size_;
    std::atomic<size_t> peak_used_size_;
    std::atomic<size_t> allocation_count_;
    std::atomic<size_t> free_count_;
    std::atomic<size_t> failed_allocations_;
    std::array<std::atomic<size_t>, NUM_LAYERS> layer_allocations_;
    std::atomic<size_t> large_allocations_;

    bool initialized_;
    // Thread-local cache invalidation keys:
    // - instance_id_: unique per LayeredMemoryPool object lifetime (even if stack address is reused)
    // - epoch_: increments on initialize/destroy to invalidate caches across re-init
    uint64_t instance_id_;
    std::atomic<uint64_t> epoch_;

public:
    LayeredMemoryPool();
    ~LayeredMemoryPool();

    // 禁止拷贝和移动
    LayeredMemoryPool(const LayeredMemoryPool&) = delete;
    LayeredMemoryPool& operator=(const LayeredMemoryPool&) = delete;
    LayeredMemoryPool(LayeredMemoryPool&&) = delete;
    LayeredMemoryPool& operator=(LayeredMemoryPool&&) = delete;

    /**
     * 初始化内存池
     * @param config 配置参数
     * @return 是否成功
     */
    bool initialize(const MemoryPoolConfig& config);

    /**
     * 销毁内存池，释放所有资源
     */
    void destroy();

    /**
     * 分配内存
     * @param size 请求的大小
     * @return 分配的内存指针，失败返回 nullptr
     */
    void* allocate(size_t size);

    /**
     * 重新分配内存
     * @param ptr 原指针
     * @param new_size 新大小
     * @return 新的内存指针，失败返回 nullptr
     */
    void* reallocate(void* ptr, size_t new_size);

    /**
     * 分配并清零内存
     * @param count 元素数量
     * @param size 每个元素的大小
     * @return 分配的内存指针，失败返回 nullptr
     */
    void* callocate(size_t count, size_t size);

    /**
     * 释放内存
     * @param ptr 要释放的指针
     */
    void deallocate(void* ptr);

    /**
     * 分配对齐的内存
     * @param alignment 对齐要求
     * @param size 请求的大小
     * @return 对齐的内存指针，失败返回 nullptr
     */
    void* allocate_aligned(size_t alignment, size_t size);

    /**
     * 释放对齐的内存
     * @param ptr 对齐的指针
     */
    void deallocate_aligned(void* ptr);

    /**
     * 获取统计信息
     */
    Stats get_stats() const;

    /**
     * 检查指针是否属于此内存池
     */
    bool owns(void* ptr) const;

    /**
     * 检查是否已初始化
     */
    bool is_initialized() const { return initialized_; }

    /**
     * 获取全局单例
     */
    static LayeredMemoryPool& instance();

    /**
     * 将此内存池注册为 roaring 的内存分配器
     */
    void register_with_roaring();

private:
    // Localized template helpers: compile out logging code paths while keeping runtime toggle.
    template <bool EnableLogging>
    void* allocate_impl(size_t size);

    template <bool EnableLogging>
    void* reallocate_impl(void* ptr, size_t new_size);

    template <bool EnableLogging>
    void* callocate_impl(size_t count, size_t size);

    template <bool EnableLogging>
    void* allocate_aligned_impl(size_t alignment, size_t size);

    /**
     * 根据大小获取对应的层级索引
     * @return 层级索引，-1 表示需要使用大块区域
     */
    int get_layer_index(size_t size) const;

    /**
     * 初始化小块区域
     */
    void init_small_blocks();

    /**
     * 初始化大块区域
     */
    void init_large_blocks();

    /**
     * 从小块池分配
     */
    void* allocate_small(size_t size);

    /**
     * 从大块区域分配
     */
    void* allocate_large(size_t size);

    /**
     * 释放小块
     */
    void deallocate_small(void* ptr, int layer_index);

    /**
     * 释放大块
     */
    void deallocate_large(void* ptr);

    /**
     * 获取指针对应的层级索引
     */
    int get_layer_for_ptr(void* ptr) const;

    /**
     * 获取指针对应的大块头
     */
    LargeBlockHeader* get_large_block_header(void* ptr) const;

    /**
     * 尝试合并相邻的空闲大块
     */
    void coalesce_large_blocks(LargeBlockHeader* block);

    /**
     * 验证大块头部指针是否有效
     */
    bool is_valid_large_block_header(LargeBlockHeader* header) const;
};

// 全局内存 hook 函数声明（用于 roaring）
extern "C" {
void* roaring_pool_malloc(size_t size);
void* roaring_pool_realloc(void* ptr, size_t size);
void* roaring_pool_calloc(size_t count, size_t size);
void roaring_pool_free(void* ptr);
void* roaring_pool_aligned_malloc(size_t alignment, size_t size);
void roaring_pool_aligned_free(void* ptr);
}
