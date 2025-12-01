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
//   https://github.com/apache/incubator-doris/blob/master/be/src/olap/rowset/segment_v2/bitmap_index_writer.cpp

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

#include "storage/rowset/bitmap_index_writer.h"

#include <map>
#include <memory>
#include <roaring/roaring.hh>
#include <utility>

#include "fs/fs.h"
#include "runtime/mem_pool.h"
#include "storage/olap_type_infra.h"
#include "storage/rowset/common.h"
#include "storage/rowset/encoding_info.h"
#include "storage/rowset/indexed_column_writer.h"
#include "storage/type_traits.h"
#include "storage/types.h"
#include "types/bitmap_value_detail.h"
#include "util/faststring.h"
#include "util/phmap/btree.h"
#include "util/phmap/phmap.h"
#include "util/slice.h"
#include "util/utf8.h"
#include "util/xxh3.h"

namespace starrocks {

// mapping from ValueType to RoaringType
template <typename ValueType>
struct BitmapValueTraits;

template <>
struct BitmapValueTraits<rowid_t> {
    using type = roaring::Roaring;
};

template <>
struct BitmapValueTraits<uint64_t> {
    using type = detail::Roaring64Map;
};

template <typename ValueType>
class BitmapUpdateContext {
    static constexpr size_t estimate_size_threshold = 1024;

    using RoaringType = BitmapValueTraits<ValueType>::type;

public:
    explicit BitmapUpdateContext(ValueType rid) : _roaring(RoaringType::bitmapOf(1, rid)) {
        _pending_adds.reserve(_ADD_BATCH_SIZE);
    }
    explicit BitmapUpdateContext(ValueType rid0, ValueType rid1) : _roaring(RoaringType::bitmapOfList({rid0, rid1})) {
        _pending_adds.reserve(_ADD_BATCH_SIZE);
    }

    RoaringType* roaring() { return &_roaring; }

    static uint64_t estimate_size(int element_count) {
        // When _element_count is less than estimate_size_threshold, we use
        // (1 + _element_count + 1) * (sizeof(ValueType)) to approximately estimate true size of roaring bitmap:
        // one bit pre    4 bytes         4 bytes *  _element_count
        // [ 1            cardinality      data ]
        return (1 + sizeof(ValueType) * (element_count + 1));
    }

    static void init_estimate_size(uint64_t* reverted_index_size) { *reverted_index_size += estimate_size(1); }

    void add_and_flush_if_needed(ValueType rid) {
        _pending_adds.push_back(rid);
        if (_pending_adds.size() >= _ADD_BATCH_SIZE) {
            flush_pending_adds();
        }
    }

    void flush_pending_adds() {
        if (!_pending_adds.empty()) {
            _roaring.addMany(_pending_adds.size(), _pending_adds.data());
            _pending_adds.clear();
        }
    }

    // When _element_count is less than estimate_size_threshold, update the estimate size
    // When _element_count equals to estimate_size_threshold, clear previous estimate size, disable estimation.
    // When _element_count is larger than estimate_size_threshold, use `getSizeInBytes(false)` to get
    // the exact size of roaring bitmap. For efficiency, we will not update the roaring's size each time when size changed.
    // We will save the sized changed roaring bitmap in _late_update_context_vector, and delay calculation of update size
    // each time when `size()` of bitmap is called.
    // Return value in this function indicates whether this BitmapUpdateContext needs to be added to the _late_update_context_vector
    bool update_estimate_size(uint64_t* reverted_index_size) {
        bool need_add = false;
        _element_count++;
        if (_element_count < estimate_size_threshold) {
            *reverted_index_size += sizeof(ValueType);
        } else if (_element_count == estimate_size_threshold) {
            *reverted_index_size -= BitmapUpdateContext::estimate_size(_element_count);
            _size_changed = true;
            need_add = true;
        } else {
            // Add BitmapUpdateContext to _late_update_context_vector iff
            // it hash not been added to _late_update_context_vector before.
            if (!_size_changed) {
                need_add = true;
            }
            _size_changed = true;
        }
        return need_add;
    }

    void late_update_size(uint64_t* reverted_index_size) {
        uint64_t current_size = _roaring.getSizeInBytes(false);
        *reverted_index_size += (current_size - _previous_size);
        _previous_size = current_size;
        _size_changed = false;
    }

private:
    RoaringType _roaring;
    uint64_t _previous_size{0};
    uint32_t _element_count{1};
    bool _size_changed{false};
    std::vector<ValueType> _pending_adds;
    static constexpr size_t _ADD_BATCH_SIZE = 64;
};

// if last bit is 0 it is std::unique_ptr<BitmapUpdateContext>
// else it is a single value
template <typename ValueType>
class BitmapUpdateContextRefOrSingleValue {
    using RoaringType = BitmapValueTraits<ValueType>::type;

public:
    BitmapUpdateContextRefOrSingleValue(const BitmapUpdateContextRefOrSingleValue& rhs) = delete;
    BitmapUpdateContextRefOrSingleValue& operator=(const BitmapUpdateContextRefOrSingleValue& rhs) = delete;
    BitmapUpdateContextRefOrSingleValue(BitmapUpdateContextRefOrSingleValue&& rhs) noexcept {
        _value = rhs._value;
        rhs._value = 1; // make sure not delete when rhs is destroyed
    }
    BitmapUpdateContextRefOrSingleValue& operator=(BitmapUpdateContextRefOrSingleValue&& rhs) noexcept {
        this->_value = rhs._value;
        rhs._value = 1; // make sure not delete when rhs is destroyed
        return *this;
    }
    BitmapUpdateContextRefOrSingleValue(ValueType value) { _value = (value << 1) | 1; }
    ~BitmapUpdateContextRefOrSingleValue() {
        if (is_context()) {
            delete context();
        }
    }
    bool is_context() const { return (_value & 1) == 0; }
    ValueType value() const { return _value >> 1; }
    BitmapUpdateContext<ValueType>* context() const {
        return reinterpret_cast<BitmapUpdateContext<ValueType>*>(_value); // NOLINT
    }
    void add(ValueType rid) {
        if (is_context()) {
            context()->add_and_flush_if_needed(rid);
        } else {
            auto* context = new BitmapUpdateContext<ValueType>(value(), rid);
            _value = reinterpret_cast<uint64_t>(context); // NOLINT
        }
    }
    RoaringType* roaring() { return context()->roaring(); }

    static uint64_t estimate_size(int element_count) {
        return BitmapUpdateContext<ValueType>::estimate_size(element_count);
    }

    static void init_estimate_size(uint64_t* reverted_index_size) {
        return BitmapUpdateContext<ValueType>::init_estimate_size(reverted_index_size);
    }

    bool update_estimate_size(uint64_t* reverted_index_size) {
        if (context()) {
            return context()->update_estimate_size(reverted_index_size);
        } else {
            return false;
        }
    }

    void late_update_size(uint64_t* reverted_index_size) {
        if (is_context()) {
            context()->late_update_size(reverted_index_size);
        }
    }

    void flush_pending_adds() {
        if (is_context()) {
            context()->flush_pending_adds();
        }
    }

private:
    uint64_t _value;
};

struct BitmapIndexSliceHash {
    inline size_t operator()(const Slice& v) const { return XXH3_64bits(v.data, v.size); }
};

template <typename CppType, bool positional>
struct BitmapIndexTraits {
    using ValueType = rowid_t;
    using BitmapUpdateContextType = BitmapUpdateContextRefOrSingleValue<ValueType>;
    using UnorderedMemoryIndexType = phmap::flat_hash_map<CppType, BitmapUpdateContextType>;
    using OrderedMemoryIndexType = std::map<CppType, BitmapUpdateContextType>;
};

template <>
struct BitmapIndexTraits<Slice, false> {
    using ValueType = rowid_t;
    using BitmapUpdateContextType = BitmapUpdateContextRefOrSingleValue<ValueType>;
    using UnorderedMemoryIndexType =
            phmap::flat_hash_map<Slice, BitmapUpdateContextType, BitmapIndexSliceHash, std::equal_to<Slice>>;
    using OrderedMemoryIndexType = std::map<Slice, BitmapUpdateContextType, Slice::Comparator>;
};

template <>
struct BitmapIndexTraits<Slice, true> {
    using ValueType = uint64_t;
    using BitmapUpdateContextType = BitmapUpdateContextRefOrSingleValue<ValueType>;
    using UnorderedMemoryIndexType =
            phmap::flat_hash_map<Slice, BitmapUpdateContextType, BitmapIndexSliceHash, std::equal_to<Slice>>;
    using OrderedMemoryIndexType = std::map<Slice, BitmapUpdateContextType, Slice::Comparator>;
};

// Builder for bitmap index. Bitmap index is comprised of two parts
// - an "ordered dictionary" which contains all distinct values of a column and maps each value to an id.
//   the smallest value mapped to 0, second value mapped to 1, ..
// - a posting list which stores one bitmap for each value in the dictionary. each bitmap is used to represent
//   the list of rowid where a particular value exists.
//
// E.g, if the column contains 10 rows ['x', 'x', 'x', 'b', 'b', 'b', 'x', 'b', 'b', 'b'],
// then the ordered dictionary would be ['b', 'x'] which maps 'b' to 0 and 'x' to 1,
// and the posting list would contain two bitmaps
//   bitmap for ID 0 : [0 0 0 1 1 1 0 1 1 1]
//   bitmap for ID 1 : [1 1 1 0 0 0 1 0 0 0]
//   the n-th bit is set to 1 if the n-th row equals to the corresponding value.
//
template <LogicalType field_type, bool positional>
class BitmapIndexWriterImpl : public BitmapIndexWriter {
public:
    using CppType = CppTypeTraits<field_type>::CppType;
    using ValueType = BitmapIndexTraits<CppType, positional>::ValueType;
    using RoaringType = BitmapValueTraits<ValueType>::type;
    using BitmapUpdateContextType = BitmapIndexTraits<CppType, positional>::BitmapUpdateContextType;
    using UnorderedMemoryIndexType = BitmapIndexTraits<CppType, positional>::UnorderedMemoryIndexType;
    using OrderedMemoryIndexType = BitmapIndexTraits<CppType, positional>::OrderedMemoryIndexType;

    explicit BitmapIndexWriterImpl(TypeInfoPtr type_info, int32_t gram_num)
            : _gram_num(gram_num), _typeinfo(std::move(type_info)) {}

    ~BitmapIndexWriterImpl() override = default;

    void add_values(const void* values, size_t count) override {
        auto p = static_cast<const CppType*>(values);
        for (size_t i = 0; i < count; ++i) {
            add_value_with_current_rowid(p);
            incre_rowid();
            ++p;
        }
    }

    inline void add_value_with_current_rowid(const void* vptr) override {
        const CppType& value = *static_cast<const CppType*>(vptr);

        ValueType val = _rid;
        if constexpr (positional) {
            val = val << 32 | _pos;
            ++_pos;
        }

        if constexpr (std::is_same_v<CppType, Slice>) {
            LOG(INFO) << "##### dict " << value.to_string() << ", write value: " << val;
        }

        auto it = _mem_index.find(value);
        if (it != _mem_index.end()) {
            it->second.add(val);
            if (it->second.update_estimate_size(&_reverted_index_size)) {
                _late_update_context_vector.push_back(it->second.context());
            }
        } else {
            // new value, copy value and insert new key->bitmap pair
            CppType new_value;
            _typeinfo->deep_copy(&new_value, &value, &_pool);
            _mem_index.emplace(new_value, val);
            BitmapUpdateContext<ValueType>::init_estimate_size(&_reverted_index_size);
        }
    }

    void add_nulls(uint32_t count) override {
        _null_bitmap.addRange(_rid, _rid + count);
        _rid += count;
        _pos = 0;
    }

    Status finish(WritableFile* wfile, ColumnIndexMetaPB* index_meta) override {
        index_meta->set_type(BITMAP_INDEX);
        BitmapIndexPB* meta = index_meta->mutable_bitmap_index();
        return finish(wfile, meta);
    }

    Status finish(WritableFile* wfile, BitmapIndexPB* meta) override {
        meta->set_bitmap_type(BitmapIndexPB::ROARING_BITMAP);
        meta->set_has_null(!_null_bitmap.isEmpty());

        OrderedMemoryIndexType ordered_mem_index;
        for (auto& p : _mem_index) {
            p.second.flush_pending_adds();
            ordered_mem_index.insert(std::move(p));
        }

        // write dictionary
        RETURN_IF_ERROR(_write_dictionary(ordered_mem_index, wfile, meta->mutable_dict_column()));
        // write bitmap
        RETURN_IF_ERROR(_write_bitmap(ordered_mem_index, wfile, meta->mutable_bitmap_column()));

        if constexpr (field_type == TYPE_VARCHAR || field_type == TYPE_CHAR) {
            if (_gram_num > 0) {
                size_t offset = 0;
                OrderedMemoryIndexType ngram_index;
                for (const auto& it : ordered_mem_index) {
                    RETURN_IF_ERROR(_build_ngram(ngram_index, &it.first, offset++));
                }
                for (auto& it : ngram_index) {
                    it.second.flush_pending_adds();
                }
                RETURN_IF_ERROR(_write_dictionary(ngram_index, wfile, meta->mutable_ngram_dict_column()));
                RETURN_IF_ERROR(_write_bitmap(ngram_index, wfile, meta->mutable_ngram_bitmap_column(), false));
            }
        }
        return Status::OK();
    }

    uint64_t size() const override {
        uint64_t size = 0;
        size += _null_bitmap.getSizeInBytes(false);
        for (BitmapUpdateContext<ValueType>* update_context : _late_update_context_vector) {
            update_context->flush_pending_adds();
            update_context->late_update_size(&_reverted_index_size);
        }
        _late_update_context_vector.clear();
        size += _reverted_index_size;
        size += _mem_index.size() * sizeof(CppType);
        size += _pool.total_allocated_bytes();
        return size;
    }

    inline void incre_rowid() override {
        ++_rid;
        _pos = 0;
    }

private:
    Status _build_ngram(OrderedMemoryIndexType& ngram_index, const Slice* cur_slice, const size_t offset) {
        if (_gram_num <= 0) {
            return Status::InvalidArgument(
                    "Invalid gram num while building ngram index for inverted index dictionary.");
        }

        std::vector<size_t> index;
        const size_t slice_gram_num = get_utf8_index(*cur_slice, &index);

        for (size_t j = 0; j + _gram_num <= slice_gram_num; ++j) {
            // find next ngram
            size_t cur_ngram_length =
                    j + _gram_num < slice_gram_num ? index[j + _gram_num] - index[j] : cur_slice->get_size() - index[j];
            Slice cur_ngram(cur_slice->data + index[j], cur_ngram_length);

            // add this ngram into set
            auto it = ngram_index.find(cur_ngram);
            if (it == ngram_index.end()) {
                CppType new_value;
                _typeinfo->deep_copy(&new_value, &cur_ngram, &_pool);
                ngram_index.emplace(new_value, offset);
            } else {
                it->second.add(offset);
            }
        }
        return Status::OK();
    }

    Status _write_dictionary(OrderedMemoryIndexType& ordered_mem_index, WritableFile* wfile,
                             IndexedColumnMetaPB* meta) {
        IndexedColumnWriterOptions options;
        options.write_ordinal_index = true;
        options.write_value_index = true;
        options.encoding = EncodingInfo::get_default_encoding(_typeinfo->type(), true);
        options.compression = _dictionary_compression;

        IndexedColumnWriter dict_column_writer(options, _typeinfo, wfile);
        RETURN_IF_ERROR(dict_column_writer.init());
        for (auto const& it : ordered_mem_index) {
            RETURN_IF_ERROR(dict_column_writer.add(&(it.first)));
        }
        return dict_column_writer.finish(meta);
    }

    Status _write_bitmap(OrderedMemoryIndexType& ordered_mem_index, WritableFile* wfile, IndexedColumnMetaPB* meta,
                         bool write_null = true) {
        std::vector<BitmapUpdateContextType*> bitmaps;
        for (auto& it : ordered_mem_index) {
            bitmaps.push_back(&(it.second));
        }

        uint32_t max_bitmap_size = 0;
        std::vector<uint32_t> bitmap_sizes;
        for (auto& bitmap : bitmaps) {
            uint32_t bitmap_size = 0;
            if (bitmap->is_context()) {
                bitmap->context()->roaring()->runOptimize();
                bitmap_size = bitmap->context()->roaring()->getSizeInBytes(false);
                if (max_bitmap_size < bitmap_size) {
                    max_bitmap_size = bitmap_size;
                }
            }
            bitmap_sizes.push_back(bitmap_size);
        }

        TypeInfoPtr bitmap_typeinfo = get_type_info(TYPE_OBJECT);

        IndexedColumnWriterOptions options;
        options.write_ordinal_index = true;
        options.write_value_index = false;
        options.encoding = EncodingInfo::get_default_encoding(bitmap_typeinfo->type(), false);
        // we already store compressed bitmap, use NO_COMPRESSION to save some cpu
        options.compression = NO_COMPRESSION;

        IndexedColumnWriter bitmap_column_writer(options, bitmap_typeinfo, wfile);
        RETURN_IF_ERROR(bitmap_column_writer.init());

        faststring buf;
        buf.reserve(max_bitmap_size);
        for (size_t i = 0; i < bitmaps.size(); ++i) {
            if (bitmaps[i]->is_context()) {
                buf.resize(bitmap_sizes[i]); // so that buf[0..size) can be read and written
                if constexpr (std::is_same_v<RoaringType, roaring::Roaring>) {
                    bitmaps[i]->context()->roaring()->write(reinterpret_cast<char*>(buf.data()), false);
                } else {
                    bitmaps[i]->context()->roaring()->write(reinterpret_cast<char*>(buf.data()),
                                                            config::bitmap_serialize_version);
                }
            } else {
                RoaringType roar = RoaringType::bitmapOfList({bitmaps[i]->value()});
                roar.runOptimize();
                auto sz = roar.getSizeInBytes(false);
                buf.resize(sz);
                if constexpr (std::is_same_v<RoaringType, roaring::Roaring>) {
                    roar.write(reinterpret_cast<char*>(buf.data()), false);
                } else {
                    roar.write(reinterpret_cast<char*>(buf.data()), config::bitmap_serialize_version);
                }
            }
            Slice buf_slice(buf);
            RETURN_IF_ERROR(bitmap_column_writer.add(&buf_slice));
        }
        if (write_null && !_null_bitmap.isEmpty()) {
            _null_bitmap.runOptimize();
            buf.resize(_null_bitmap.getSizeInBytes(false)); // so that buf[0..size) can be read and written
            _null_bitmap.write(reinterpret_cast<char*>(buf.data()), false);
            Slice buf_slice(buf);
            RETURN_IF_ERROR(bitmap_column_writer.add(&buf_slice));
        }
        return bitmap_column_writer.finish(meta);
    }

    int32_t _gram_num;

    TypeInfoPtr _typeinfo;
    rowid_t _rid = 0;
    rowid_t _pos = 0;

    // row id list for null value
    roaring::Roaring _null_bitmap;
    // unique value to its row id list
    // Use UnorderedMemoryIndexType during loading and sort it when finish is more efficient than only
    // use OrderedMemoryIndexType. Especially for the case of built-in inverted index workload.
    UnorderedMemoryIndexType _mem_index;
    MemPool _pool;

    // roaring bitmap size
    mutable uint64_t _reverted_index_size = 0;
    mutable std::vector<BitmapUpdateContext<ValueType>*> _late_update_context_vector;
};

struct BitmapIndexWriterBuilder {
    template <LogicalType ftype>
    std::unique_ptr<BitmapIndexWriter> operator()(const TypeInfoPtr& typeinfo, int32_t gram_num, bool with_position) {
        if constexpr (ftype == TYPE_VARCHAR || ftype == TYPE_CHAR) {
            if (with_position) {
                return std::make_unique<BitmapIndexWriterImpl<ftype, true>>(typeinfo, gram_num);
            }
        }
        return std::make_unique<BitmapIndexWriterImpl<ftype, false>>(typeinfo, gram_num);
    }
};

Status BitmapIndexWriter::create(const TypeInfoPtr& typeinfo, std::unique_ptr<BitmapIndexWriter>* res, int32_t gram_num,
                                 bool with_position) {
    LogicalType type = typeinfo->type();
    *res = field_type_dispatch_bitmap_index(type, BitmapIndexWriterBuilder(), typeinfo, gram_num, with_position);

    return Status::OK();
}

} // namespace starrocks
