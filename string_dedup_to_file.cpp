// Five dedup strategies with build time, total time, and estimated memory (no RSS).
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// =============================
// 偏移信息结构
// =============================
struct OffsetInfo {
    uint32_t first_pos{};
    std::vector<uint32_t> offsets;

    OffsetInfo() = default;
    explicit OffsetInfo(const uint32_t pos) : first_pos(pos) {}

    [[nodiscard]] size_t memory_usage() const {
        return sizeof(first_pos) + sizeof(offsets) + offsets.capacity() * sizeof(uint32_t);
    }
};

// =============================
// 随机字符串生成器（正态分布长度）
// =============================
class RandomStringGenerator {
    std::mt19937 gen;
    std::normal_distribution<double> normal_dist;
    std::uniform_int_distribution<int> char_dist;
    int min_len, max_len;

public:
    explicit RandomStringGenerator(const int seed = 42, const double mean = 20.0, const double std_dev = 10.0,
                                   const int min_len = 5, const int max_len = 80)
            : gen(seed), normal_dist(mean, std_dev), char_dist('a', 'z'), min_len(min_len), max_len(max_len) {}

    std::string operator()() {
        int len;
        do {
            len = static_cast<int>(std::round(normal_dist(gen)));
        } while (len < min_len || len > max_len);

        std::string s;
        s.reserve(len);
        for (int i = 0; i < len; ++i) {
            s += static_cast<char>(char_dist(gen));
        }
        return s;
    }
};

// =============================
// 字节转 MB 宏
// =============================
#define B_TO_MB(bytes) (static_cast<double>(bytes) / (1024.0 * 1024.0))

// =============================
// 结果结构体
// =============================
struct TimingMemoryResult {
    int64_t build_ms;
    int64_t total_ms;
    double memory_mb;
    size_t unique_count;
};

// =============================
// 写入函数模板
// =============================

void write_output(const std::string& filename, const std::vector<std::string>& keys,
                  const std::unordered_map<std::string, OffsetInfo>& data_map) {
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "❌ Cannot open: " << filename << "\n";
        return;
    }

    for (const auto& k : keys) {
        out << k << "\n";
    }
    out << "---offsets---\n";
    for (const auto& k : keys) {
        const auto& offs = data_map.at(k).offsets;
        for (size_t i = 0; i < offs.size(); ++i) {
            if (i > 0) out << " ";
            out << offs[i];
        }
        out << "\n";
    }
    out.close();
}

void write_output_from_map(const std::string& filename, const std::map<std::string, OffsetInfo>& sorted_map) {
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "❌ Cannot open: " << filename << "\n";
        return;
    }

    std::vector<std::string> keys;
    keys.reserve(sorted_map.size());
    for (const auto& [key, _] : sorted_map) {
        out << key << "\n";
        keys.push_back(key);
    }
    out << "---offsets---\n";
    for (const auto& k : keys) {
        const auto& offs = sorted_map.at(k).offsets;
        for (size_t i = 0; i < offs.size(); ++i) {
            if (i > 0) out << " ";
            out << offs[i];
        }
        out << "\n";
    }
    out.close();
}

void write_output_ptr(const std::string& filename, const std::vector<const std::string*>& sorted_ptrs,
                      const std::unordered_map<const std::string*, OffsetInfo>& index) {
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "❌ Cannot open: " << filename << "\n";
        return;
    }

    for (const auto* ptr : sorted_ptrs) {
        out << *ptr << "\n";
    }
    out << "---offsets---\n";
    for (const auto* ptr : sorted_ptrs) {
        const auto& offs = index.at(ptr).offsets;
        for (size_t i = 0; i < offs.size(); ++i) {
            if (i > 0) out << " ";
            out << offs[i];
        }
        out << "\n";
    }
    out.close();
}

// =============================
// 方案 A: Hash-Then-Sort using string* pointers（零拷贝排序）
// =============================
TimingMemoryResult run_A_hash_with_pointers(const std::vector<std::string>& input, const std::string& output_file) {
    std::cout << "\n🚀 Running [A] Hash + string* pointers...\n";

    std::unordered_map<std::string, OffsetInfo> hash_table;
    hash_table.reserve(input.size() / 2);
    size_t total_string_bytes = 0;

    const auto build_start = std::chrono::steady_clock::now();

    for (uint32_t pos = 0; pos < input.size(); ++pos) {
        const auto& s = input[pos];
        if (auto it = hash_table.find(s); it == hash_table.end()) {
            hash_table[s] = OffsetInfo(pos);
            total_string_bytes += s.capacity();
        } else {
            it->second.offsets.push_back(pos - it->second.first_pos);
        }
    }

    // 提取指针并排序
    std::vector<const std::string*> sorted_ptrs;
    sorted_ptrs.reserve(hash_table.size());
    for (const auto& [key, _] : hash_table) {
        sorted_ptrs.push_back(&key);
    }
    std::sort(sorted_ptrs.begin(), sorted_ptrs.end(),
              [](const std::string* a, const std::string* b) { return *a < *b; });

    const auto build_end = std::chrono::steady_clock::now();

    // 估算内存
    size_t info_memory = 0;
    for (const auto& [_, info] : hash_table) {
        info_memory += info.memory_usage();
    }
    const double estimated_memory = B_TO_MB(total_string_bytes +               // 所有字符串存储
                                            info_memory +                      // 所有 OffsetInfo 数据
                                            hash_table.size() * 32 +           // unordered_map 节点开销（哈希控制块等）
                                            sorted_ptrs.size() * sizeof(void*) // 指针数组
    );

    // 写入文件
    std::vector<std::string> keys_for_write;
    keys_for_write.reserve(sorted_ptrs.size());
    for (const auto* ptr : sorted_ptrs) {
        keys_for_write.push_back(*ptr);
    }
    const auto write_start = build_end;
    write_output(output_file, keys_for_write, hash_table);
    const auto total_end = std::chrono::steady_clock::now();

    const int64_t build_time = std::chrono::duration_cast<std::chrono::milliseconds>(build_end - build_start).count();
    const int64_t total_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(total_end - write_start).count() + build_time;

    std::cout << "✅ Build Time: " << build_time << " ms\n";
    std::cout << "📊 Estimated Memory: ~" << estimated_memory << " MB\n";

    return {build_time, total_time, estimated_memory, hash_table.size()};
}

// =============================
// 方案 B: unordered → map
// =============================
TimingMemoryResult run_B_unordered_to_map(const std::vector<std::string>& input, const std::string& output_file) {
    std::cout << "\n🚀 Running [B] unordered → map...\n";

    std::unordered_map<std::string, OffsetInfo> temp_map;
    temp_map.reserve(input.size() / 2);
    size_t total_string_bytes = 0;

    const auto build_start = std::chrono::steady_clock::now();

    for (uint32_t pos = 0; pos < input.size(); ++pos) {
        const auto& s = input[pos];
        if (auto it = temp_map.find(s); it == temp_map.end()) {
            temp_map[s] = OffsetInfo(pos);
            total_string_bytes += s.capacity();
        } else {
            it->second.offsets.push_back(pos - it->second.first_pos);
        }
    }

    const std::map sorted_map(temp_map.begin(), temp_map.end());
    const auto build_end = std::chrono::steady_clock::now();

    // 估算内存
    size_t info_memory = 0;
    size_t map_string_bytes = 0;
    for (const auto& [key, info] : sorted_map) {
        info_memory += info.memory_usage();
        map_string_bytes += key.capacity();
    }
    const size_t rb_tree_overhead = sorted_map.size() * (3 * sizeof(void*) + sizeof(bool));       // 红黑树指针
    const double estimated_memory = B_TO_MB(total_string_bytes +                                  // 原始插入时的 string
                                            map_string_bytes +                                    // map 中又存了一份
                                            info_memory + rb_tree_overhead + temp_map.size() * 32 // unordered_map 开销
    );

    // 写入
    const auto write_start = build_end;
    write_output_from_map(output_file, sorted_map);
    const auto total_end = std::chrono::steady_clock::now();

    const int64_t build_time = std::chrono::duration_cast<std::chrono::milliseconds>(build_end - build_start).count();
    const int64_t total_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(total_end - write_start).count() + build_time;

    std::cout << "✅ Build Time: " << build_time << " ms\n";
    std::cout << "📊 Estimated Memory: ~" << estimated_memory << " MB\n";

    return {build_time, total_time, estimated_memory, sorted_map.size()};
}

// =============================
// 方案 C: String Pool + Ptr Map
// =============================
TimingMemoryResult run_C_string_pool_ptr(const std::vector<std::string>& input, const std::string& output_file) {
    std::cout << "\n🚀 Running [C] String Pool + Ptr Map...\n";

    std::unordered_set<std::string> string_pool;
    std::unordered_map<const std::string*, OffsetInfo> index;
    string_pool.reserve(input.size() / 2);

    auto build_start = std::chrono::steady_clock::now();

    size_t total_string_bytes = 0;

    for (uint32_t pos = 0; pos < input.size(); ++pos) {
        const auto& s = input[pos];
        auto [key_ptr, inserted] = string_pool.insert(s);
        const std::string* key = &*key_ptr;

        if (inserted) {
            index[key] = OffsetInfo(pos);
            total_string_bytes += s.capacity();
        } else {
            auto& info = index[key];
            info.offsets.push_back(pos - info.first_pos);
        }
    }

    std::vector<const std::string*> sorted_ptrs;
    sorted_ptrs.reserve(index.size());
    for (const auto& [key, _] : index) {
        sorted_ptrs.push_back(key);
    }
    std::sort(sorted_ptrs.begin(), sorted_ptrs.end(),
              [](const std::string* a, const std::string* b) { return *a < *b; });

    auto build_end = std::chrono::steady_clock::now();

    // 估算内存
    size_t info_memory = 0;
    for (const auto& [_, info] : index) {
        info_memory += info.memory_usage();
    }
    size_t set_node_overhead = string_pool.size() * 32;
    size_t ptr_map_overhead = index.size() * (sizeof(void*) + 32);
    double estimated_memory =
            B_TO_MB(total_string_bytes + // 字符串池
                    info_memory + set_node_overhead + ptr_map_overhead + sorted_ptrs.size() * sizeof(void*));

    // 写入
    auto write_start = build_end;
    write_output_ptr(output_file, sorted_ptrs, index);
    auto total_end = std::chrono::steady_clock::now();

    int64_t build_time = std::chrono::duration_cast<std::chrono::milliseconds>(build_end - build_start).count();
    int64_t total_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(total_end - write_start).count() + build_time;

    std::cout << "✅ Build Time: " << build_time << " ms\n";
    std::cout << "📊 Estimated Memory: ~" << estimated_memory << " MB\n";

    return {build_time, total_time, estimated_memory, index.size()};
}

// =============================
// 方案 D: Direct std::map
// =============================
TimingMemoryResult run_D_direct_map(const std::vector<std::string>& input, const std::string& output_file) {
    std::cout << "\n🚀 Running [D] Direct std::map...\n";

    std::map<std::string, OffsetInfo> sorted_map;
    size_t total_string_bytes = 0;

    const auto build_start = std::chrono::steady_clock::now();

    for (uint32_t pos = 0; pos < input.size(); ++pos) {
        const auto& s = input[pos];
        if (auto it = sorted_map.find(s); it == sorted_map.end()) {
            sorted_map[s] = OffsetInfo(pos);
            total_string_bytes += s.capacity();
        } else {
            it->second.offsets.push_back(pos - it->second.first_pos);
        }
    }

    const auto build_end = std::chrono::steady_clock::now();

    // 估算内存
    size_t info_memory = 0;
    size_t rb_tree_overhead = 0;
    for (const auto& [key, info] : sorted_map) {
        info_memory += info.memory_usage();
        rb_tree_overhead += 3 * sizeof(void*) + sizeof(bool);
    }
    const double estimated_memory = B_TO_MB(total_string_bytes + info_memory + rb_tree_overhead);

    // 写入
    const auto write_start = build_end;
    write_output_from_map(output_file, sorted_map);
    const auto total_end = std::chrono::steady_clock::now();

    const int64_t build_time = std::chrono::duration_cast<std::chrono::milliseconds>(build_end - build_start).count();
    const int64_t total_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(total_end - write_start).count() + build_time;

    std::cout << "✅ Build Time: " << build_time << " ms\n";
    std::cout << "📊 Estimated Memory: ~" << estimated_memory << " MB\n";

    return {build_time, total_time, estimated_memory, sorted_map.size()};
}

// =============================
// 方案 E: Hash-Then-Sort with string copy
// =============================
TimingMemoryResult run_E_hash_copy_strings(const std::vector<std::string>& input, const std::string& output_file) {
    std::cout << "\n🚀 Running [E] Hash + copy strings...\n";

    std::unordered_map<std::string, OffsetInfo> hash_table;
    hash_table.reserve(input.size() / 2);
    size_t total_string_bytes = 0;

    const auto build_start = std::chrono::steady_clock::now();

    for (uint32_t pos = 0; pos < input.size(); ++pos) {
        const auto& s = input[pos];
        if (auto it = hash_table.find(s); it == hash_table.end()) {
            hash_table[s] = OffsetInfo(pos);
            total_string_bytes += s.capacity();
        } else {
            it->second.offsets.push_back(pos - it->second.first_pos);
        }
    }

    std::vector<std::string> sorted_keys;
    sorted_keys.reserve(hash_table.size());
    for (const auto& [key, _] : hash_table) {
        sorted_keys.push_back(key);
    }
    std::sort(sorted_keys.begin(), sorted_keys.end());

    const auto build_end = std::chrono::steady_clock::now();

    // 估算内存
    size_t info_memory = 0;
    for (const auto& [_, info] : hash_table) {
        info_memory += info.memory_usage();
    }
    const double estimated_memory = B_TO_MB(total_string_bytes +                           // 字符串存储
                                            info_memory + (hash_table.size() * 32) +       // 哈希表节点开销
                                            (sorted_keys.capacity() * sizeof(std::string)) // 排序 vector
    );

    // 写入
    const auto write_start = build_end;
    write_output(output_file, sorted_keys, hash_table);
    const auto total_end = std::chrono::steady_clock::now();

    const int64_t build_time = std::chrono::duration_cast<std::chrono::milliseconds>(build_end - build_start).count();
    const int64_t total_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(total_end - write_start).count() + build_time;

    std::cout << "✅ Build Time: " << build_time << " ms\n";
    std::cout << "📊 Estimated Memory: ~" << estimated_memory << " MB\n";

    return {build_time, total_time, estimated_memory, hash_table.size()};
}

// =============================
// 主函数
// =============================
int main() {
    constexpr size_t total_strings = 10000000;
    constexpr double duplicate_ratio = 0.2;

    std::cout << "🔧 Generating " << total_strings << " strings with length ~N(20,10) ∈ [5,80]...\n";

    RandomStringGenerator gen(42, 20.0, 10.0, 5, 80);
    std::vector<std::string> raw_input;
    raw_input.reserve(total_strings);

    constexpr auto vocab_size = static_cast<size_t>(total_strings * (1.0 - duplicate_ratio));
    std::vector<std::string> vocab;
    vocab.reserve(vocab_size);

    for (size_t i = 0; i < vocab_size; ++i) {
        vocab.push_back(gen());
    }

    std::mt19937 rng(123);
    std::uniform_int_distribution<size_t> pick(0, vocab.size() - 1);
    for (size_t i = 0; i < total_strings; ++i) {
        raw_input.push_back(vocab[pick(rng)]);
    }

    std::cout << "✅ Generated. Expected unique: ~" << vocab_size << "\n";

    // === 运行五种方案 ===
    const auto [a_build_ms, a_total_ms, a_memory_mb, a_unique_count] =
            run_A_hash_with_pointers(raw_input, "output_A.txt");
    const auto [b_build_ms, b_total_ms, b_memory_mb, b_unique_count] =
            run_B_unordered_to_map(raw_input, "output_B.txt");
    const auto [c_build_ms, c_total_ms, c_memory_mb, c_unique_count] = run_C_string_pool_ptr(raw_input, "output_C.txt");
    const auto [d_build_ms, d_total_ms, d_memory_mb, d_unique_count] = run_D_direct_map(raw_input, "output_D.txt");
    const auto [e_build_ms, e_total_ms, e_memory_mb, e_unique_count] =
            run_E_hash_copy_strings(raw_input, "output_E.txt");

    // === 汇总表格 ===
    std::cout << "\n" << std::string(85, '=') << "\n";
    std::cout << "📊 FINAL PERFORMANCE SUMMARY — 5 STRATEGIES\n";
    std::cout << std::string(85, '=') << "\n";
    printf("%-4s %-18s %-12s %-12s %-12s %-10s\n", "ID", "Method", "Build(ms)", "Total(ms)", "Est(MB)", "Unique");
    printf("%-4s %-18s %-12d %-12d %-12.2f %-10zu\n", "A", "Hash+Ptr", static_cast<int>(a_build_ms),
           static_cast<int>(a_total_ms), a_memory_mb, a_unique_count);
    printf("%-4s %-18s %-12d %-12d %-12.2f %-10zu\n", "B", "Unord→Map", static_cast<int>(b_build_ms),
           static_cast<int>(b_total_ms), b_memory_mb, b_unique_count);
    printf("%-4s %-18s %-12d %-12d %-12.2f %-10zu\n", "C", "StrPool+Ptr", static_cast<int>(c_build_ms),
           static_cast<int>(c_total_ms), c_memory_mb, c_unique_count);
    printf("%-4s %-18s %-12d %-12d %-12.2f %-10zu\n", "D", "std::map", static_cast<int>(d_build_ms),
           static_cast<int>(d_total_ms), d_memory_mb, d_unique_count);
    printf("%-4s %-18s %-12d %-12d %-12.2f %-10zu\n", "E", "Hash+Copy", static_cast<int>(e_build_ms),
           static_cast<int>(e_total_ms), e_memory_mb, e_unique_count);

    std::cout << "\n🎉 All 5 cases completed! Results saved to output_[A-E].txt\n";
    return 0;
}
