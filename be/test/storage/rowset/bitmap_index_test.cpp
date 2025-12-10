// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// This file is based on code available under the Apache license here:
//   https://github.com/apache/incubator-doris/blob/master/be/test/olap/rowset/segment_v2/bitmap_index_test.cpp

// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <gtest/gtest.h>

#include <string>
#include <thread>

#include "column/column_viewer.h"
#include "fs/fs_memory.h"
#include "runtime/mem_pool.h"
#include "runtime/mem_tracker.h"
#include "storage/chunk_helper.h"
#include "storage/key_coder.h"
#include "storage/olap_common.h"
#include "storage/rowset/bitmap_index_reader.h"
#include "storage/rowset/bitmap_index_writer.h"
#include "storage/types.h"
#include "testutil/assert.h"
#include "util/utf8.h"

namespace starrocks {

class BitmapIndexTest : public testing::Test {
public:
    const std::string kTestDir = "/bitmap_index_test";

    BitmapIndexTest() = default;

protected:
    void SetUp() override {
        _fs = std::make_shared<MemoryFileSystem>();
        ASSERT_TRUE(_fs->create_dir(kTestDir).ok());

        _opts.use_page_cache = true;
        _opts.stats = &_stats;
    }
    void TearDown() override {}

    void get_bitmap_reader_iter(RandomAccessFile* rfile, const ColumnIndexMetaPB& meta, BitmapIndexReader** reader,
                                BitmapIndexIterator** iter, int32_t gram_num = -1, bool with_position = false) {
        _opts.read_file = rfile;
        *reader = new BitmapIndexReader(gram_num, with_position);
        ASSIGN_OR_ABORT(auto r, (*reader)->load(_opts, meta.bitmap_index()));
        ASSERT_TRUE(r);
        ASSERT_OK((*reader)->new_iterator(_opts, iter));
    }

    template <LogicalType type>
    void write_index_file(std::string& filename, const void* values, size_t value_count, size_t null_count,
                          ColumnIndexMetaPB* meta) {
        TypeInfoPtr type_info = get_type_info(type);
        {
            ASSIGN_OR_ABORT(auto wfile, _fs->new_writable_file(filename));

            std::unique_ptr<BitmapIndexWriter> writer;
            BitmapIndexWriter::create(type_info, &writer);
            writer->add_values(values, value_count);
            writer->add_nulls(null_count);
            ASSERT_TRUE(writer->finish(wfile.get(), meta).ok());
            ASSERT_EQ(BITMAP_INDEX, meta->type());
            ASSERT_TRUE(wfile->close().ok());
        }
    }

    void write_index_file_use_by_gin(const int32_t gram_num, const std::string& filename,
                                     const std::vector<std::string>& values, ColumnIndexMetaPB* meta) const {
        const TypeInfoPtr type_info = get_type_info(TYPE_VARCHAR);
        ASSIGN_OR_ABORT(const auto wfile, _fs->new_writable_file(filename));

        std::unique_ptr<BitmapIndexWriter> writer;
        BitmapIndexWriter::create(type_info, &writer, gram_num);

        for (size_t i = 0; i < values.size(); ++i) {
            Slice tmp(values[i]);
            writer->add_value_with_current_rowid(&tmp);
        }
        ASSERT_TRUE(writer->finish(wfile.get(), meta).ok());
        ASSERT_EQ(BITMAP_INDEX, meta->type());
        ASSERT_TRUE(wfile->close().ok());
    }

    std::shared_ptr<MemoryFileSystem> _fs = nullptr;
    MemPool _pool;
    IndexReadOptions _opts;
    OlapReaderStatistics _stats;
};

TEST_F(BitmapIndexTest, test_invert) {
    size_t num_uint8_rows = 1024 * 10;
    int* val = new int[num_uint8_rows];
    for (int i = 0; i < num_uint8_rows; ++i) {
        val[i] = i;
    }

    std::string file_name = kTestDir + "/invert";
    ColumnIndexMetaPB meta;
    write_index_file<TYPE_INT>(file_name, val, num_uint8_rows, 0, &meta);
    {
        BitmapIndexReader* reader = nullptr;
        BitmapIndexIterator* iter = nullptr;
        ASSIGN_OR_ABORT(auto rfile, _fs->new_random_access_file(file_name));
        get_bitmap_reader_iter(rfile.get(), meta, &reader, &iter);

        int value = 2;
        bool exact_match;
        Status st = iter->seek_dictionary(&value, &exact_match);
        ASSERT_TRUE(st.ok());
        ASSERT_TRUE(exact_match);
        ASSERT_EQ(2, iter->current_ordinal());

        Roaring bitmap;
        iter->read_bitmap(iter->current_ordinal(), &bitmap);
        ASSERT_TRUE(Roaring::bitmapOf(1, 2) == bitmap);

        int value2 = 1024 * 9;
        st = iter->seek_dictionary(&value2, &exact_match);
        ASSERT_TRUE(st.ok());
        ASSERT_TRUE(exact_match);
        ASSERT_EQ(1024 * 9, iter->current_ordinal());

        iter->read_union_bitmap(iter->current_ordinal(), iter->bitmap_nums(), &bitmap);
        ASSERT_EQ(1025, bitmap.cardinality());

        int value3 = 1024;
        iter->seek_dictionary(&value3, &exact_match);
        ASSERT_EQ(1024, iter->current_ordinal());

        Roaring bitmap2;
        iter->read_union_bitmap(0, iter->current_ordinal(), &bitmap2);
        ASSERT_EQ(1024, bitmap2.cardinality());

        delete reader;
        delete iter;
    }
    delete[] val;
}

TEST_F(BitmapIndexTest, test_invert_2) {
    size_t num_uint8_rows = 1024 * 10;
    int* val = new int[num_uint8_rows];
    for (int i = 0; i < 1024; ++i) {
        val[i] = i;
    }

    for (int i = 1024; i < num_uint8_rows; ++i) {
        val[i] = i * 10;
    }

    std::string file_name = kTestDir + "/invert2";
    ColumnIndexMetaPB meta;
    write_index_file<TYPE_INT>(file_name, val, num_uint8_rows, 0, &meta);

    {
        BitmapIndexReader* reader = nullptr;
        BitmapIndexIterator* iter = nullptr;
        ASSIGN_OR_ABORT(auto rfile, _fs->new_random_access_file(file_name));
        get_bitmap_reader_iter(rfile.get(), meta, &reader, &iter);

        int value = 1026;
        bool exact_match;
        auto st = iter->seek_dictionary(&value, &exact_match);
        ASSERT_TRUE(st.ok());
        ASSERT_TRUE(!exact_match);

        ASSERT_EQ(1024, iter->current_ordinal());

        Roaring bitmap;
        iter->read_union_bitmap(0, iter->current_ordinal(), &bitmap);
        ASSERT_EQ(1024, bitmap.cardinality());

        delete reader;
        delete iter;
    }
    delete[] val;
}

TEST_F(BitmapIndexTest, test_multi_pages) {
    size_t num_uint8_rows = 1024 * 1024;
    auto* val = new int64_t[num_uint8_rows];
    for (int i = 0; i < num_uint8_rows; ++i) {
        val[i] = random() + 10000;
    }
    val[1024 * 510] = 2019;

    std::string file_name = kTestDir + "/mul";
    ColumnIndexMetaPB meta;
    write_index_file<TYPE_BIGINT>(file_name, val, num_uint8_rows, 0, &meta);
    {
        BitmapIndexReader* reader = nullptr;
        BitmapIndexIterator* iter = nullptr;
        ASSIGN_OR_ABORT(auto rfile, _fs->new_random_access_file(file_name));
        get_bitmap_reader_iter(rfile.get(), meta, &reader, &iter);

        int64_t value = 2019;
        bool exact_match;
        auto st = iter->seek_dictionary(&value, &exact_match);
        ASSERT_TRUE(st.ok()) << "status:" << st.to_string();
        ASSERT_EQ(0, iter->current_ordinal());

        Roaring bitmap;
        iter->read_bitmap(iter->current_ordinal(), &bitmap);
        ASSERT_EQ(1, bitmap.cardinality());

        delete reader;
        delete iter;
    }
    delete[] val;
}

TEST_F(BitmapIndexTest, test_null) {
    size_t num_uint8_rows = 1024;
    auto* val = new int64_t[num_uint8_rows];
    for (int i = 0; i < num_uint8_rows; ++i) {
        val[i] = i;
    }

    std::string file_name = kTestDir + "/null";
    ColumnIndexMetaPB meta;
    write_index_file<TYPE_BIGINT>(file_name, val, num_uint8_rows, 30, &meta);
    {
        BitmapIndexReader* reader = nullptr;
        BitmapIndexIterator* iter = nullptr;
        ASSIGN_OR_ABORT(auto rfile, _fs->new_random_access_file(file_name));
        get_bitmap_reader_iter(rfile.get(), meta, &reader, &iter);

        Roaring bitmap;
        iter->read_null_bitmap(&bitmap);
        ASSERT_EQ(30, bitmap.cardinality());

        delete reader;
        delete iter;
    }
    delete[] val;
}

TEST_F(BitmapIndexTest, test_concurrent_load) {
    size_t num_uint8_rows = 1024;
    auto* val = new int64_t[num_uint8_rows];
    for (int i = 0; i < num_uint8_rows; ++i) {
        val[i] = i;
    }

    std::string file_name = kTestDir + "/null";
    ColumnIndexMetaPB meta;
    write_index_file<TYPE_BIGINT>(file_name, val, num_uint8_rows, 30, &meta);

    IndexReadOptions opts;
    ASSIGN_OR_ABORT(auto rfile, _fs->new_random_access_file(file_name))
    opts.read_file = rfile.get();
    opts.use_page_cache = true;
    OlapReaderStatistics stats;
    opts.stats = &stats;
    auto reader = std::make_unique<BitmapIndexReader>();
    std::atomic<int> count{0};
    std::atomic<int> loads{0};
    constexpr int kNumThreads = 5;
    std::vector<std::thread> threads;
    for (int i = 0; i < kNumThreads; i++) {
        threads.emplace_back([&]() {
            count.fetch_add(1);
            while (count.load() < count) {
                ;
            }
            ASSIGN_OR_ABORT(auto first_load, reader->load(opts, meta.bitmap_index()));
            loads.fetch_add(first_load);
        });
    }
    for (auto&& t : threads) {
        t.join();
    }
    ASSERT_EQ(1, loads.load());

    BitmapIndexIterator* iter = nullptr;
    ASSERT_OK(reader->new_iterator(opts, &iter));

    Roaring bitmap;
    iter->read_null_bitmap(&bitmap);
    ASSERT_EQ(30, bitmap.cardinality());

    delete iter;
    delete[] val;
}

TEST_F(BitmapIndexTest, test_dict_ngram_index) {
    constexpr int32_t num_keywords = 10;
    constexpr int32_t gram_num = 3;

    std::set<std::string> ngram;
    std::vector<std::string> keywords;
    for (int i = 0; i < num_keywords; ++i) {
        // slice should be one of hel,ell,llo,low,wor,orl,rld,ld ,d {0,...,9}
        const std::string keyword = "hello, world " + std::to_string(i);

        std::vector<size_t> index;
        Slice cur_slice(keyword);
        const size_t slice_gram_num = get_utf8_index(cur_slice, &index);

        for (size_t j = 0; j + gram_num <= slice_gram_num; ++j) {
            // find next ngram
            size_t cur_ngram_length =
                    j + gram_num < slice_gram_num ? index[j + gram_num] - index[j] : cur_slice.get_size() - index[j];
            Slice cur_ngram(cur_slice.data + index[j], cur_ngram_length);
            ngram.emplace(cur_ngram.to_string());
        }

        keywords.emplace_back(keyword);
    }

    std::string file_name = kTestDir + "/dict_ngram_index";
    ColumnIndexMetaPB meta;
    write_index_file_use_by_gin(3, file_name, keywords, &meta);

    {
        BitmapIndexReader* reader = nullptr;
        BitmapIndexIterator* iter = nullptr;
        ASSIGN_OR_ABORT(const auto rfile, _fs->new_random_access_file(file_name));
        get_bitmap_reader_iter(rfile.get(), meta, &reader, &iter, 3);

        const size_t dict_num = reader->bitmap_nums();
        ASSERT_EQ(dict_num, num_keywords);

        const size_t ngram_num = reader->ngram_bitmap_nums();
        ASSERT_EQ(ngram_num, ngram.size());

        size_t to_read = ngram_num;
        const auto col = ChunkHelper::column_from_field_type(TYPE_VARCHAR, false);
        ASSERT_TRUE(iter->next_batch_ngram(0, &to_read, col.get()).ok());
        ASSERT_EQ(ngram_num, to_read);

        ColumnViewer<TYPE_VARCHAR> viewer(std::move(col));
        ASSERT_EQ(ngram.size(), viewer.size());

        auto it = ngram.begin();
        for (rowid_t i = 0; i < viewer.size(); ++i) {
            auto value = viewer.value(i);
            auto current_gram = *it;
            ASSERT_EQ(current_gram, value.to_string());
            ++it;

            roaring::Roaring r1, r2;
            ASSERT_TRUE(iter->read_ngram_bitmap(i, &r1).ok());
            ASSERT_TRUE(iter->seek_dict_by_ngram(&value, &r2).ok());
            ASSERT_EQ(r1, r2);
            if (current_gram.starts_with("d ")) {
                ASSERT_EQ(1, r1.cardinality());
            } else {
                ASSERT_EQ(num_keywords, r1.cardinality());
            }
        }

        delete reader;
        delete iter;
    }
}

TEST_F(BitmapIndexTest, test_with_position) {
    // Test BitmapIndexWriter with position tracking enabled
    // When position is enabled, each value is stored as (rowid << 32 | position)
    const TypeInfoPtr type_info = get_type_info(TYPE_VARCHAR);
    const std::string file_name = kTestDir + "/with_position";

    // Create test data: multiple values per row
    std::vector<std::vector<std::string>> rows{
            {"hello", "world", "test"},
            {"hello", "foo", "bar"},
            {"world", "test"},
    };

    ColumnIndexMetaPB meta;
    {
        ASSIGN_OR_ABORT(auto wfile, _fs->new_writable_file(file_name));

        std::unique_ptr<BitmapIndexWriter> writer;
        // Create writer with position tracking (gram_num=-1, with_position=true)
        BitmapIndexWriter::create(type_info, &writer, -1, true);

        for (const auto& row : rows) {
            for (const auto& val : row) {
                Slice slice(val);
                writer->add_value_with_current_rowid(&slice);
            }
            writer->incre_rowid();
        }

        ASSERT_TRUE(writer->finish(wfile.get(), &meta).ok());
        ASSERT_EQ(BITMAP_INDEX, meta.type());
        ASSERT_TRUE(wfile->close().ok());
    }

    {
        BitmapIndexReader* reader = nullptr;
        BitmapIndexIterator* iter = nullptr;
        ASSIGN_OR_ABORT(auto rfile, _fs->new_random_access_file(file_name));
        get_bitmap_reader_iter(rfile.get(), meta, &reader, &iter, -1, true);

        // Verify the dictionary contains unique values
        ASSERT_EQ(5, reader->bitmap_nums()); // bar, foo, hello, test, world (sorted)

        for (int row_id = 0; row_id < rows.size(); ++row_id) {
            for (int pos_idx = 0; pos_idx < rows[row_id].size(); ++pos_idx) {
                Slice slice(rows[row_id][pos_idx]);

                bool exact_match;
                auto st = iter->seek_dictionary(&slice, &exact_match);

                ASSERT_TRUE(st.ok());
                ASSERT_TRUE(exact_match);

                detail::Roaring64Map bitmap;
                ASSERT_TRUE(iter->read_bitmap64(iter->current_ordinal(), &bitmap).ok());

                roaring::Roaring row_ids = bitmap.getAllHighBits();
                ASSERT_TRUE(row_ids.contains(row_id));
                roaring::Roaring positions = bitmap.getLowBitsRoaring(row_id);
                ASSERT_TRUE(positions.contains(pos_idx));
            }
        }

        delete reader;
        delete iter;
    }
}

TEST_F(BitmapIndexTest, test_with_position_ngram) {
    // Test BitmapIndexWriter with both position tracking and ngram indexing enabled
    // This combines two features:
    // 1. Position tracking: each value stored as (rowid << 32 | position)
    // 2. Ngram indexing: creates inverted index for character n-grams
    const TypeInfoPtr type_info = get_type_info(TYPE_VARCHAR);
    const std::string file_name = kTestDir + "/with_position_ngram";
    constexpr int32_t gram_num = 2; // Use bigrams for testing

    // Create test data: multiple values per row with overlapping n-grams
    std::vector<std::vector<std::string>> rows{
            {"hello", "help", "world"}, // row 0: has "he", "el", "ll", "lo", "he", "el", "lp", "wo", "or", "rl", "ld"
            {"hero", "hello"},          // row 1: has "he", "er", "ro", "he", "el", "ll", "lo"
            {"help", "world", "hero"},  // row 2: has "he", "el", "lp", "wo", "or", "rl", "ld", "he", "er", "ro"
    };

    ColumnIndexMetaPB meta;
    {
        ASSIGN_OR_ABORT(auto wfile, _fs->new_writable_file(file_name));

        std::unique_ptr<BitmapIndexWriter> writer;
        // Create writer with both position tracking and ngram indexing
        BitmapIndexWriter::create(type_info, &writer, gram_num, true);

        for (const auto& row : rows) {
            for (const auto& val : row) {
                Slice slice(val);
                writer->add_value_with_current_rowid(&slice);
            }
            writer->incre_rowid();
        }

        ASSERT_TRUE(writer->finish(wfile.get(), &meta).ok());
        ASSERT_EQ(BITMAP_INDEX, meta.type());
        ASSERT_TRUE(wfile->close().ok());
    }

    {
        BitmapIndexReader* reader = nullptr;
        BitmapIndexIterator* iter = nullptr;
        ASSIGN_OR_ABORT(auto rfile, _fs->new_random_access_file(file_name));
        get_bitmap_reader_iter(rfile.get(), meta, &reader, &iter, gram_num, true);

        // Verify the dictionary contains unique values (sorted)
        // Unique values: "hello", "help", "hero", "world"
        ASSERT_EQ(4, reader->bitmap_nums());

        // Verify ngram dictionary was created
        const size_t ngram_num = reader->ngram_bitmap_nums();
        ASSERT_GT(ngram_num, 0); // Should have multiple n-grams

        // Test 1: Verify position tracking for main dictionary
        // "hello" appears at: row0:pos0, row1:pos1
        {
            Slice hello_slice("hello");
            bool exact_match;
            auto st = iter->seek_dictionary(&hello_slice, &exact_match);
            ASSERT_TRUE(st.ok());
            ASSERT_TRUE(exact_match);

            detail::Roaring64Map bitmap;
            ASSERT_TRUE(iter->read_bitmap64(iter->current_ordinal(), &bitmap).ok());

            // Check it appears in row 0 and row 1
            roaring::Roaring row_ids = bitmap.getAllHighBits();
            ASSERT_EQ(2, row_ids.cardinality());
            ASSERT_TRUE(row_ids.contains(0));
            ASSERT_TRUE(row_ids.contains(1));

            // Check positions: row0:pos0, row1:pos1
            roaring::Roaring positions_row0 = bitmap.getLowBitsRoaring(0);
            ASSERT_EQ(1, positions_row0.cardinality());
            ASSERT_TRUE(positions_row0.contains(0));

            roaring::Roaring positions_row1 = bitmap.getLowBitsRoaring(1);
            ASSERT_EQ(1, positions_row1.cardinality());
            ASSERT_TRUE(positions_row1.contains(1));
        }

        // Test 2: Verify position tracking for "help"
        // "help" appears at: row0:pos1, row2:pos0
        {
            Slice help_slice("help");
            bool exact_match;
            auto st = iter->seek_dictionary(&help_slice, &exact_match);
            ASSERT_TRUE(st.ok());
            ASSERT_TRUE(exact_match);

            detail::Roaring64Map bitmap;
            ASSERT_TRUE(iter->read_bitmap64(iter->current_ordinal(), &bitmap).ok());

            roaring::Roaring row_ids = bitmap.getAllHighBits();
            ASSERT_EQ(2, row_ids.cardinality());
            ASSERT_TRUE(row_ids.contains(0));
            ASSERT_TRUE(row_ids.contains(2));

            // Check positions: row0:pos1, row2:pos0
            roaring::Roaring positions_row0 = bitmap.getLowBitsRoaring(0);
            ASSERT_EQ(1, positions_row0.cardinality());
            ASSERT_TRUE(positions_row0.contains(1));

            roaring::Roaring positions_row2 = bitmap.getLowBitsRoaring(2);
            ASSERT_EQ(1, positions_row2.cardinality());
            ASSERT_TRUE(positions_row2.contains(0));
        }

        // Test 3: Read and verify ngram index
        // The ngram "he" should map to dictionary offsets for words containing "he"
        // "hello" (offset 0), "help" (offset 1), "hero" (offset 2) all contain "he"
        {
            size_t to_read = ngram_num;
            const auto col = ChunkHelper::column_from_field_type(TYPE_VARCHAR, false);
            ASSERT_TRUE(iter->next_batch_ngram(0, &to_read, col.get()).ok());
            ASSERT_EQ(ngram_num, to_read);

            ColumnViewer<TYPE_VARCHAR> viewer(std::move(col));
            ASSERT_EQ(ngram_num, viewer.size());

            // Find the "he" ngram in the ngram dictionary
            bool found_he = false;
            for (rowid_t i = 0; i < viewer.size(); ++i) {
                auto ngram_value = viewer.value(i);
                if (ngram_value.to_string() == "he") {
                    found_he = true;

                    // Read the ngram bitmap - it should map to dictionary entries (not positions)
                    roaring::Roaring ngram_bitmap;
                    ASSERT_TRUE(iter->read_ngram_bitmap(i, &ngram_bitmap).ok());

                    // "he" appears in "hello" (dict offset 0), "help" (dict offset 1), "hero" (dict offset 2)
                    ASSERT_EQ(3, ngram_bitmap.cardinality());
                    ASSERT_TRUE(ngram_bitmap.contains(0)); // "hello"
                    ASSERT_TRUE(ngram_bitmap.contains(1)); // "help"
                    ASSERT_TRUE(ngram_bitmap.contains(2)); // "hero"
                    break;
                }
            }
            ASSERT_TRUE(found_he) << "N-gram 'he' should exist in ngram dictionary";
        }

        // Test 4: Verify ngram "wo" which appears in "world"
        {
            Slice wo_slice("wo");
            roaring::Roaring wo_bitmap;
            ASSERT_TRUE(iter->seek_dict_by_ngram(&wo_slice, &wo_bitmap).ok());

            // "wo" only appears in "world" which is at dictionary offset 3
            ASSERT_EQ(1, wo_bitmap.cardinality());
            ASSERT_TRUE(wo_bitmap.contains(3)); // "world"
        }

        // Test 5: Combined test - use ngram to find words, then check positions
        // Find all words containing "el" using ngram, then verify their positions
        {
            Slice el_slice("el");
            roaring::Roaring el_bitmap;
            ASSERT_TRUE(iter->seek_dict_by_ngram(&el_slice, &el_bitmap).ok());

            // "el" appears in "hello" (offset 0) and "help" (offset 1)
            ASSERT_EQ(2, el_bitmap.cardinality());
            ASSERT_TRUE(el_bitmap.contains(0)); // "hello"
            ASSERT_TRUE(el_bitmap.contains(1)); // "help"

            // Now verify positions for "hello" using position-aware bitmap
            Slice hello_slice("hello");
            bool exact_match;
            iter->seek_dictionary(&hello_slice, &exact_match);
            ASSERT_TRUE(exact_match);

            detail::Roaring64Map position_bitmap;
            ASSERT_TRUE(iter->read_bitmap64(iter->current_ordinal(), &position_bitmap).ok());

            // "hello" at row0:pos0 and row1:pos1
            ASSERT_EQ(2, position_bitmap.cardinality());
        }

        delete reader;
        delete iter;
    }
}

} // namespace starrocks
