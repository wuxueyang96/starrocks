#include "memory_pool.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {

// Thread-local cache for small blocks to reduce mutex contention.
// Important: this cache is keyed to a single pool instance_id + epoch per thread.
struct ThreadSmallBlockCache {
    uint64_t instance_id = 0;
    uint64_t epoch = 0;
    std::array<LayeredMemoryPool::FreeBlock*, LayeredMemoryPool::NUM_LAYERS> heads{};
    std::array<size_t, LayeredMemoryPool::NUM_LAYERS> counts{};
};

thread_local ThreadSmallBlockCache g_tls_cache;

constexpr size_t kLocalRefillBatch = 32;
constexpr size_t kLocalCacheLimit = 128;

inline void tls_cache_reset_if_needed(uint64_t instance_id, uint64_t epoch) {
    if (g_tls_cache.instance_id != instance_id || g_tls_cache.epoch != epoch) {
        g_tls_cache.instance_id = instance_id;
        g_tls_cache.epoch = epoch;
        for (size_t i = 0; i < LayeredMemoryPool::NUM_LAYERS; ++i) {
            g_tls_cache.heads[i] = nullptr;
            g_tls_cache.counts[i] = 0;
        }
    }
}

inline LayeredMemoryPool::FreeBlock* tls_pop(int layer) {
    auto* head = g_tls_cache.heads[layer];
    if (!head) {
        return nullptr;
    }
    g_tls_cache.heads[layer] = head->next;
    --g_tls_cache.counts[layer];
    return head;
}

inline void tls_push(int layer, LayeredMemoryPool::FreeBlock* block) {
    block->next = g_tls_cache.heads[layer];
    g_tls_cache.heads[layer] = block;
    ++g_tls_cache.counts[layer];
}

} // namespace

LayeredMemoryPool& LayeredMemoryPool::instance() {
    static LayeredMemoryPool pool;
    return pool;
}

template <bool EnableLogging>
void* LayeredMemoryPool::allocate_impl(size_t size) {
    void* result = nullptr;
    int layer = -1;

    if constexpr (EnableLogging) {
        const char* fail_reason = nullptr;

        do {
            if (!initialized_) {
                fail_reason = "pool not initialized";
                break;
            }

            if (size == 0) {
                fail_reason = "size is zero";
                break;
            }

            layer = get_layer_index(size);
            result = (layer >= 0) ? allocate_small(size) : allocate_large(size);

            if (result) {
                ++allocation_count_;
                size_t current_used = used_size_.load();
                size_t peak = peak_used_size_.load();
                while (current_used > peak && !peak_used_size_.compare_exchange_weak(peak, current_used)) {
                }
            } else {
                fail_reason = layer >= 0 ? "small block pool exhausted" : "large block pool exhausted";
                ++failed_allocations_;
            }
        } while (false);

        if (result) {
            std::printf("[MemoryPool] allocate: size=%zu, ptr=%p\n", size, result);
        } else {
            std::printf("[MemoryPool] allocate: size=%zu, ptr=null, reason=%s\n", size,
                        fail_reason ? fail_reason : "unknown");
        }
        return result;
    } else {
        if (!initialized_ || size == 0) {
            return nullptr;
        }

        layer = get_layer_index(size);
        result = (layer >= 0) ? allocate_small(size) : allocate_large(size);

        if (result) {
            ++allocation_count_;
            size_t current_used = used_size_.load();
            size_t peak = peak_used_size_.load();
            while (current_used > peak && !peak_used_size_.compare_exchange_weak(peak, current_used)) {
            }
        } else {
            ++failed_allocations_;
        }

        return result;
    }
}

template <bool EnableLogging>
void* LayeredMemoryPool::reallocate_impl(void* ptr, size_t new_size) {
    void* result = nullptr;

    if constexpr (EnableLogging) {
        const char* fail_reason = nullptr;

        do {
            if (!ptr) {
                result = allocate_impl<true>(new_size);
                if (!result) {
                    fail_reason = "allocate for null ptr failed";
                }
                break;
            }

            if (new_size == 0) {
                deallocate(ptr);
                fail_reason = "new_size is zero (deallocated)";
                break;
            }

            if (!owns(ptr)) {
                result = std::realloc(ptr, new_size);
                if (!result) {
                    fail_reason = "system realloc failed";
                }
                break;
            }

            size_t old_size = 0;
            int layer = get_layer_for_ptr(ptr);
            if (layer >= 0) {
                old_size = BLOCK_SIZES[layer];
            } else {
                LargeBlockHeader* header = get_large_block_header(ptr);
                if (header) {
                    old_size = header->size - sizeof(LargeBlockHeader);
                }
            }

            if (new_size <= old_size) {
                result = ptr;
                break;
            }

            void* new_ptr = allocate_impl<true>(new_size);
            if (!new_ptr) {
                fail_reason = "allocate new memory failed";
                break;
            }

            std::memcpy(new_ptr, ptr, old_size);
            deallocate(ptr);
            result = new_ptr;
        } while (false);

        if (result) {
            std::printf("[MemoryPool] reallocate: old_ptr=%p, new_size=%zu, new_ptr=%p\n", ptr, new_size, result);
        } else {
            std::printf("[MemoryPool] reallocate: old_ptr=%p, new_size=%zu, new_ptr=null, reason=%s\n", ptr, new_size,
                        fail_reason ? fail_reason : "unknown");
        }
        return result;
    } else {
        if (!ptr) {
            return allocate_impl<false>(new_size);
        }

        if (new_size == 0) {
            deallocate(ptr);
            return nullptr;
        }

        if (!owns(ptr)) {
            return std::realloc(ptr, new_size);
        }

        size_t old_size = 0;
        int layer = get_layer_for_ptr(ptr);
        if (layer >= 0) {
            old_size = BLOCK_SIZES[layer];
        } else {
            LargeBlockHeader* header = get_large_block_header(ptr);
            if (header) {
                old_size = header->size - sizeof(LargeBlockHeader);
            }
        }

        if (new_size <= old_size) {
            return ptr;
        }

        void* new_ptr = allocate_impl<false>(new_size);
        if (!new_ptr) {
            return nullptr;
        }

        std::memcpy(new_ptr, ptr, old_size);
        deallocate(ptr);
        return new_ptr;
    }
}

template <bool EnableLogging>
void* LayeredMemoryPool::callocate_impl(size_t count, size_t size) {
    size_t total_size = count * size;
    void* result = allocate_impl<EnableLogging>(total_size);
    if (result) {
        std::memset(result, 0, total_size);
    }

    if constexpr (EnableLogging) {
        if (result) {
            std::printf("[MemoryPool] callocate: count=%zu, size=%zu, total=%zu, ptr=%p\n", count, size, total_size,
                        result);
        } else {
            std::printf("[MemoryPool] callocate: count=%zu, size=%zu, total=%zu, ptr=null, reason=%s\n", count, size,
                        total_size, "allocate failed");
        }
    }
    return result;
}

template <bool EnableLogging>
void* LayeredMemoryPool::allocate_aligned_impl(size_t alignment, size_t size) {
    void* result = nullptr;

    if constexpr (EnableLogging) {
        const char* fail_reason = nullptr;

        do {
            if (!initialized_) {
                fail_reason = "pool not initialized";
                break;
            }

            if (size == 0) {
                fail_reason = "size is zero";
                break;
            }

            if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
                fail_reason = "invalid alignment (must be power of 2)";
                break;
            }

            size_t total_size = size + alignment + sizeof(AlignedMetadata);
            void* raw_ptr = allocate_impl<true>(total_size);
            if (!raw_ptr) {
                fail_reason = "underlying allocate failed";
                break;
            }

            uintptr_t raw_addr = reinterpret_cast<uintptr_t>(raw_ptr);
            uintptr_t aligned_addr = (raw_addr + sizeof(AlignedMetadata) + alignment - 1) & ~(alignment - 1);

            AlignedMetadata* metadata = reinterpret_cast<AlignedMetadata*>(aligned_addr - sizeof(AlignedMetadata));
            metadata->original_ptr = raw_ptr;
            metadata->size = total_size;

            result = reinterpret_cast<void*>(aligned_addr);
        } while (false);

        if (result) {
            std::printf("[MemoryPool] allocate_aligned: alignment=%zu, size=%zu, ptr=%p\n", alignment, size, result);
        } else {
            std::printf("[MemoryPool] allocate_aligned: alignment=%zu, size=%zu, ptr=null, reason=%s\n", alignment,
                        size, fail_reason ? fail_reason : "unknown");
        }
        return result;
    } else {
        if (!initialized_ || size == 0) {
            return nullptr;
        }
        if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
            return nullptr;
        }

        size_t total_size = size + alignment + sizeof(AlignedMetadata);
        void* raw_ptr = allocate_impl<false>(total_size);
        if (!raw_ptr) {
            return nullptr;
        }

        uintptr_t raw_addr = reinterpret_cast<uintptr_t>(raw_ptr);
        uintptr_t aligned_addr = (raw_addr + sizeof(AlignedMetadata) + alignment - 1) & ~(alignment - 1);

        AlignedMetadata* metadata = reinterpret_cast<AlignedMetadata*>(aligned_addr - sizeof(AlignedMetadata));
        metadata->original_ptr = raw_ptr;
        metadata->size = total_size;

        return reinterpret_cast<void*>(aligned_addr);
    }
}

LayeredMemoryPool::LayeredMemoryPool()
        : memory_(nullptr),
          small_block_start_(nullptr),
          small_block_size_(0),
          large_block_start_(nullptr),
          large_block_size_(0),
          large_block_head_(nullptr),
          used_size_(0),
          peak_used_size_(0),
          allocation_count_(0),
          free_count_(0),
          failed_allocations_(0),
          large_allocations_(0),
          initialized_(false),
          instance_id_(0),
          epoch_(0) {
    static std::atomic<uint64_t> s_instance_counter{1};
    instance_id_ = s_instance_counter.fetch_add(1, std::memory_order_relaxed);

    for (size_t i = 0; i < NUM_LAYERS; ++i) {
        free_lists_[i] = nullptr;
        layer_allocations_[i] = 0;
        small_layer_starts_[i] = nullptr;
        small_layer_ends_[i] = nullptr;
    }
}

LayeredMemoryPool::~LayeredMemoryPool() {
    destroy();
}

bool LayeredMemoryPool::initialize(const MemoryPoolConfig& config) {
    if (initialized_) {
        return false;
    }

    config_ = config;
    epoch_.fetch_add(1, std::memory_order_relaxed);

    // 分配原始内存
    memory_ = static_cast<uint8_t*>(std::malloc(config_.total_size));
    if (!memory_) {
        return false;
    }

    // 计算小块和大块区域大小
    small_block_size_ = static_cast<size_t>(config_.total_size * config_.small_block_ratio);
    large_block_size_ = config_.total_size - small_block_size_;

    // 设置区域指针
    small_block_start_ = memory_;
    large_block_start_ = memory_ + small_block_size_;

    // 初始化小块区域
    init_small_blocks();

    // 初始化大块区域
    init_large_blocks();

    initialized_ = true;
    return true;
}

void LayeredMemoryPool::destroy() {
    if (!initialized_) {
        return;
    }

    epoch_.fetch_add(1, std::memory_order_relaxed);

    if (memory_) {
        std::free(memory_);
        memory_ = nullptr;
    }

    small_block_start_ = nullptr;
    small_block_size_ = 0;
    large_block_start_ = nullptr;
    large_block_size_ = 0;
    large_block_head_ = nullptr;

    for (size_t i = 0; i < NUM_LAYERS; ++i) {
        free_lists_[i] = nullptr;
        small_layer_starts_[i] = nullptr;
        small_layer_ends_[i] = nullptr;
    }

    initialized_ = false;
}

void LayeredMemoryPool::init_small_blocks() {
    // 按比例分配每层的空间
    constexpr std::array<float, NUM_LAYERS> layer_ratios = {0.25f, 0.20f, 0.15f, 0.12f, 0.10f, 0.08f, 0.06f, 0.04f};

    uint8_t* current = small_block_start_;
    size_t remaining = small_block_size_;

    for (size_t layer = 0; layer < NUM_LAYERS; ++layer) {
        small_layer_starts_[layer] = current;
        size_t layer_size = static_cast<size_t>(small_block_size_ * layer_ratios[layer]);
        size_t block_size = BLOCK_SIZES[layer];

        size_t num_blocks = layer_size / block_size;
        if (num_blocks == 0 && remaining >= block_size) {
            num_blocks = 1;
        }

        // 构建空闲链表（在空闲内存块上存储 FreeBlock）
        FreeBlock* head = nullptr;
        for (size_t i = 0; i < num_blocks && remaining >= block_size; ++i) {
            FreeBlock* block = reinterpret_cast<FreeBlock*>(current);
            block->next = head;
            head = block;
            current += block_size;
            remaining -= block_size;
        }

        free_lists_[layer] = head;
        small_layer_ends_[layer] = current;
    }
}

void LayeredMemoryPool::init_large_blocks() {
    if (large_block_size_ < sizeof(LargeBlockHeader)) {
        large_block_head_ = nullptr;
        return;
    }

    // 初始化为一个大的空闲块
    large_block_head_ = reinterpret_cast<LargeBlockHeader*>(large_block_start_);
    large_block_head_->size = large_block_size_;
    large_block_head_->in_use = false;
    large_block_head_->prev = nullptr;
    large_block_head_->next = nullptr;
}

int LayeredMemoryPool::get_layer_index(size_t size) const {
    if (size == 0) {
        return 0;
    }

    for (size_t i = 0; i < NUM_LAYERS; ++i) {
        if (size <= BLOCK_SIZES[i]) {
            return static_cast<int>(i);
        }
    }

    return -1; // 使用大块区域
}

void* LayeredMemoryPool::allocate(size_t size) {
    return config_.enable_logging ? allocate_impl<true>(size) : allocate_impl<false>(size);
}

void* LayeredMemoryPool::allocate_small(size_t size) {
    int layer = get_layer_index(size);
    if (layer < 0) {
        return nullptr;
    }

    // Per-thread cache keying: (pool pointer, epoch).
    tls_cache_reset_if_needed(instance_id_, epoch_.load(std::memory_order_relaxed));

    auto refill_from_global = [&](int l) {
        std::lock_guard<std::mutex> lock(layer_mutexes_[l]);
        FreeBlock* head = free_lists_[l];
        if (!head) {
            return;
        }

        // Detach up to kLocalRefillBatch nodes from global list.
        FreeBlock* batch_head = head;
        FreeBlock* batch_tail = head;
        size_t n = 1;
        while (n < kLocalRefillBatch && batch_tail->next) {
            batch_tail = batch_tail->next;
            ++n;
        }
        free_lists_[l] = batch_tail->next;
        batch_tail->next = nullptr;

        // Push batch into TLS list (prepend).
        batch_tail->next = g_tls_cache.heads[l];
        g_tls_cache.heads[l] = batch_head;
        g_tls_cache.counts[l] += n;
    };

    // Try current layer then higher layers; use TLS first, then refill.
    for (int i = layer; i < static_cast<int>(NUM_LAYERS); ++i) {
        if (auto* block = tls_pop(i)) {
            used_size_ += BLOCK_SIZES[i];
            ++layer_allocations_[i];
            return block;
        }
        refill_from_global(i);
        if (auto* block = tls_pop(i)) {
            used_size_ += BLOCK_SIZES[i];
            ++layer_allocations_[i];
            return block;
        }
    }

    return nullptr;
}

void* LayeredMemoryPool::allocate_large(size_t size) {
    std::lock_guard<std::mutex> lock(large_block_mutex_);

    // 计算需要的总大小（包含头部）
    size_t total_size = size + sizeof(LargeBlockHeader);

    // 首次适应算法
    LargeBlockHeader* current = large_block_head_;
    while (current) {
        if (!current->in_use && current->size >= total_size) {
            // 找到合适的块
            size_t remaining = current->size - total_size;

            // 如果剩余空间足够大，分割块
            if (remaining >= sizeof(LargeBlockHeader) + MIN_BLOCK_SIZE) {
                LargeBlockHeader* new_block =
                        reinterpret_cast<LargeBlockHeader*>(reinterpret_cast<uint8_t*>(current) + total_size);
                new_block->size = remaining;
                new_block->in_use = false;
                new_block->prev = current;
                new_block->next = current->next;

                if (current->next) {
                    current->next->prev = new_block;
                }
                current->next = new_block;
                current->size = total_size;
            }

            current->in_use = true;
            used_size_ += current->size;
            ++large_allocations_;

            return reinterpret_cast<uint8_t*>(current) + sizeof(LargeBlockHeader);
        }
        current = current->next;
    }

    return nullptr;
}

void* LayeredMemoryPool::reallocate(void* ptr, size_t new_size) {
    return config_.enable_logging ? reallocate_impl<true>(ptr, new_size) : reallocate_impl<false>(ptr, new_size);
}

void* LayeredMemoryPool::callocate(size_t count, size_t size) {
    return config_.enable_logging ? callocate_impl<true>(count, size) : callocate_impl<false>(count, size);
}

void LayeredMemoryPool::deallocate(void* ptr) {
    if (!ptr || !initialized_) {
        return;
    }

    if (!owns(ptr)) {
        // Fallback to system free for memory not owned by this pool
        std::free(ptr);
        return;
    }

    int layer = get_layer_for_ptr(ptr);
    if (layer >= 0) {
        deallocate_small(ptr, layer);
    } else {
        deallocate_large(ptr);
    }

    ++free_count_;
}

void LayeredMemoryPool::deallocate_small(void* ptr, int layer_index) {
    // 防护：验证 layer_index 范围
    if (layer_index < 0 || layer_index >= static_cast<int>(NUM_LAYERS)) {
        if (config_.enable_logging) {
            std::printf("[MemoryPool] ERROR: deallocate_small invalid layer_index=%d, ptr=%p\n", layer_index, ptr);
        }
        return;
    }

    // 防护：验证 ptr 在有效范围内
    if (!owns(ptr)) {
        if (config_.enable_logging) {
            std::printf("[MemoryPool] ERROR: deallocate_small ptr not owned, ptr=%p, layer=%d\n", ptr, layer_index);
        }
        return;
    }

    // 将内存块重新加入 TLS 缓存，降低全局锁争用
    tls_cache_reset_if_needed(instance_id_, epoch_.load(std::memory_order_relaxed));
    FreeBlock* block = static_cast<FreeBlock*>(ptr);
    tls_push(layer_index, block);

    // 超过阈值则批量回收至全局 free_list
    if (g_tls_cache.counts[layer_index] > kLocalCacheLimit) {
        size_t to_flush = g_tls_cache.counts[layer_index] - kLocalCacheLimit / 2;
        if (to_flush > kLocalRefillBatch) {
            to_flush = kLocalRefillBatch;
        }

        FreeBlock* batch_head = g_tls_cache.heads[layer_index];
        FreeBlock* batch_tail = batch_head;
        size_t n = 0;
        while (batch_tail && n + 1 < to_flush && batch_tail->next) {
            batch_tail = batch_tail->next;
            ++n;
        }
        if (batch_head && batch_tail) {
            FreeBlock* remainder = batch_tail->next;
            batch_tail->next = nullptr;
            g_tls_cache.heads[layer_index] = remainder;
            g_tls_cache.counts[layer_index] -= (n + 1);

            // flush to global
            {
                std::lock_guard<std::mutex> lock(layer_mutexes_[layer_index]);
                batch_tail->next = free_lists_[layer_index];
                free_lists_[layer_index] = batch_head;
            }
        }
    }

    used_size_ -= BLOCK_SIZES[layer_index];
}

void LayeredMemoryPool::deallocate_large(void* ptr) {
    std::lock_guard<std::mutex> lock(large_block_mutex_);

    LargeBlockHeader* header = get_large_block_header(ptr);
    if (!header) {
        if (config_.enable_logging) {
            std::printf("[MemoryPool] ERROR: deallocate_large invalid ptr=%p, header is null\n", ptr);
        }
        return;
    }

    if (!header->in_use) {
        if (config_.enable_logging) {
            std::printf("[MemoryPool] ERROR: deallocate_large double-free detected, ptr=%p\n", ptr);
        }
        return;
    }

    // 防护：验证 header->size 合理性
    if (header->size == 0 || header->size > large_block_size_) {
        if (config_.enable_logging) {
            std::printf("[MemoryPool] ERROR: deallocate_large corrupted header, ptr=%p, size=%zu\n", ptr, header->size);
        }
        return;
    }

    used_size_ -= header->size;
    header->in_use = false;

    // 尝试合并相邻的空闲块
    coalesce_large_blocks(header);
}

void LayeredMemoryPool::coalesce_large_blocks(LargeBlockHeader* block) {
    // 防护：验证 block 指针
    if (!is_valid_large_block_header(block)) {
        if (config_.enable_logging) {
            std::printf("[MemoryPool] ERROR: coalesce_large_blocks invalid block=%p\n", static_cast<void*>(block));
        }
        return;
    }

    // 与后一个块合并
    if (block->next) {
        if (!is_valid_large_block_header(block->next)) {
            if (config_.enable_logging) {
                std::printf("[MemoryPool] ERROR: coalesce_large_blocks corrupted next=%p\n",
                            static_cast<void*>(block->next));
            }
            block->next = nullptr;
        } else if (!block->next->in_use) {
            LargeBlockHeader* next_block = block->next;
            block->size += next_block->size;
            block->next = next_block->next;
            if (next_block->next) {
                next_block->next->prev = block;
            }
        }
    }

    // 与前一个块合并
    if (block->prev) {
        if (!is_valid_large_block_header(block->prev)) {
            if (config_.enable_logging) {
                std::printf("[MemoryPool] ERROR: coalesce_large_blocks corrupted prev=%p\n",
                            static_cast<void*>(block->prev));
            }
            block->prev = nullptr;
        } else if (!block->prev->in_use) {
            LargeBlockHeader* prev_block = block->prev;
            prev_block->size += block->size;
            prev_block->next = block->next;
            if (block->next) {
                block->next->prev = prev_block;
            }
        }
    }
}

void* LayeredMemoryPool::allocate_aligned(size_t alignment, size_t size) {
    return config_.enable_logging ? allocate_aligned_impl<true>(alignment, size)
                                  : allocate_aligned_impl<false>(alignment, size);
}

void LayeredMemoryPool::deallocate_aligned(void* ptr) {
    if (!ptr || !initialized_) {
        return;
    }

    // 防护：验证 ptr 在内存池范围内（先检查对齐地址本身）
    if (!owns(ptr)) {
        if (config_.enable_logging) {
            std::printf("[MemoryPool] ERROR: deallocate_aligned ptr out of range, ptr=%p\n", ptr);
        }
        return;
    }

    // 读取元数据（在对齐地址前）
    auto ptr_addr = reinterpret_cast<uintptr_t>(ptr);
    auto metadata_addr = ptr_addr - sizeof(AlignedMetadata);

    // 防护：验证 metadata 地址在有效范围内
    auto pool_start = reinterpret_cast<uintptr_t>(memory_);
    auto pool_end = pool_start + config_.total_size;
    if (metadata_addr < pool_start || metadata_addr + sizeof(AlignedMetadata) > pool_end) {
        if (config_.enable_logging) {
            std::printf("[MemoryPool] ERROR: deallocate_aligned metadata address out of range, ptr=%p, metadata=%p\n",
                        ptr, reinterpret_cast<void*>(metadata_addr));
        }
        return;
    }

    AlignedMetadata* metadata = reinterpret_cast<AlignedMetadata*>(metadata_addr);

    // 防护：验证 original_ptr 有效
    if (!owns(metadata->original_ptr)) {
        if (config_.enable_logging) {
            std::printf("[MemoryPool] ERROR: deallocate_aligned invalid original_ptr=%p, ptr=%p\n",
                        metadata->original_ptr, ptr);
        }
        return;
    }

    // 防护：验证 size 合理性
    if (metadata->size == 0 || metadata->size > config_.total_size) {
        if (config_.enable_logging) {
            std::printf("[MemoryPool] ERROR: deallocate_aligned corrupted metadata, size=%zu, ptr=%p\n", metadata->size,
                        ptr);
        }
        return;
    }

    deallocate(metadata->original_ptr);
}

bool LayeredMemoryPool::owns(void* ptr) const {
    if (!ptr) {
        return false;
    }

    if (!initialized_ || !memory_) {
        return false;
    }

    auto addr = reinterpret_cast<uintptr_t>(ptr);
    auto start = reinterpret_cast<uintptr_t>(memory_);
    auto end = start + config_.total_size;

    return addr >= start && addr < end;
}

int LayeredMemoryPool::get_layer_for_ptr(void* ptr) const {
    auto addr = reinterpret_cast<uintptr_t>(ptr);
    auto small_start = reinterpret_cast<uintptr_t>(small_block_start_);
    auto small_end = small_start + small_block_size_;

    if (addr < small_start || addr >= small_end) {
        return -1; // 大块区域
    }

    // 用 init_small_blocks() 计算出的“真实边界”判断层级，避免比例/取整导致的错误分类
    for (size_t layer = 0; layer < NUM_LAYERS; ++layer) {
        auto start = reinterpret_cast<uintptr_t>(small_layer_starts_[layer]);
        auto end = reinterpret_cast<uintptr_t>(small_layer_ends_[layer]);
        if (start == 0 || end == 0) {
            continue;
        }
        if (addr >= start && addr < end) {
            return static_cast<int>(layer);
        }
    }

    return -1;
}

LayeredMemoryPool::LargeBlockHeader* LayeredMemoryPool::get_large_block_header(void* ptr) const {
    auto addr = reinterpret_cast<uintptr_t>(ptr);
    auto large_start = reinterpret_cast<uintptr_t>(large_block_start_);
    auto large_end = large_start + large_block_size_;

    if (addr < large_start + sizeof(LargeBlockHeader) || addr >= large_end) {
        return nullptr;
    }

    return reinterpret_cast<LargeBlockHeader*>(reinterpret_cast<uint8_t*>(ptr) - sizeof(LargeBlockHeader));
}

bool LayeredMemoryPool::is_valid_large_block_header(LargeBlockHeader* header) const {
    if (!header || !initialized_) {
        return false;
    }

    auto addr = reinterpret_cast<uintptr_t>(header);
    auto large_start = reinterpret_cast<uintptr_t>(large_block_start_);
    auto large_end = large_start + large_block_size_;

    // 验证头部在大块区域内
    if (addr < large_start || addr + sizeof(LargeBlockHeader) > large_end) {
        return false;
    }

    // 验证 size 合理性
    if (header->size == 0 || header->size > large_block_size_) {
        return false;
    }

    // 验证头部 + size 不超出大块区域
    if (addr + header->size > large_end) {
        return false;
    }

    return true;
}

LayeredMemoryPool::Stats LayeredMemoryPool::get_stats() const {
    Stats stats{};
    stats.total_size = config_.total_size;
    stats.used_size = used_size_.load();
    stats.peak_used_size = peak_used_size_.load();
    stats.allocation_count = allocation_count_.load();
    stats.free_count = free_count_.load();
    stats.failed_allocations = failed_allocations_.load();

    for (size_t i = 0; i < NUM_LAYERS; ++i) {
        stats.layer_allocations[i] = layer_allocations_[i].load();
    }
    stats.large_allocations = large_allocations_.load();

    return stats;
}

void LayeredMemoryPool::register_with_roaring() {
    roaring_memory_t hooks;
    hooks.malloc = roaring_pool_malloc;
    hooks.realloc = roaring_pool_realloc;
    hooks.calloc = roaring_pool_calloc;
    hooks.free = roaring_pool_free;
    hooks.aligned_malloc = roaring_pool_aligned_malloc;
    hooks.aligned_free = roaring_pool_aligned_free;

    roaring_init_memory_hook(hooks);
}

// 全局 hook 函数实现
extern "C" {

void* roaring_pool_malloc(size_t size) {
    return LayeredMemoryPool::instance().allocate(size);
}

void* roaring_pool_realloc(void* ptr, size_t size) {
    return LayeredMemoryPool::instance().reallocate(ptr, size);
}

void* roaring_pool_calloc(size_t count, size_t size) {
    return LayeredMemoryPool::instance().callocate(count, size);
}

void roaring_pool_free(void* ptr) {
    LayeredMemoryPool::instance().deallocate(ptr);
}

void* roaring_pool_aligned_malloc(size_t alignment, size_t size) {
    return LayeredMemoryPool::instance().allocate_aligned(alignment, size);
}

void roaring_pool_aligned_free(void* ptr) {
    LayeredMemoryPool::instance().deallocate_aligned(ptr);
}

} // extern "C"
