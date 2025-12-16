#include "posting_list.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "varint_encoder.h"

// ==================== PostingList Implementation ====================

PostingList::PostingList() = default;

PostingList::~PostingList() = default;

void PostingList::addPosting(uint32_t doc_id, const uint32_t position) {
    if (const auto it = doc_to_index_.find(doc_id); it != doc_to_index_.end()) {
        // Document already exists, add position
        postings_[it->second].positions.addPosition(position);
    } else {
        // New document
        const size_t index = postings_.size();
        postings_.emplace_back(doc_id);
        postings_.back().positions.addPosition(position);
        doc_to_index_[doc_id] = index;
    }
}

const std::vector<Posting>& PostingList::getPostings() const {
    return postings_;
}

size_t PostingList::getDocFrequency() const {
    return postings_.size();
}

std::vector<uint8_t> PostingList::encode(EncodingType encoding_type) const {
    std::vector<uint8_t> encoded;

    // Encode number of postings
    VarIntEncoder::encodeValue(static_cast<uint32_t>(postings_.size()), encoded);

    if (postings_.empty()) {
        return encoded;
    }

    // Sort postings by doc_id for delta encoding
    std::vector<Posting> sorted_postings = postings_;
    std::sort(sorted_postings.begin(), sorted_postings.end(),
              [](const Posting& a, const Posting& b) { return a.doc_id < b.doc_id; });

    // Encode first doc_id
    VarIntEncoder::encodeValue(sorted_postings[0].doc_id, encoded);

    // Encode first position list
    auto pos_encoded = sorted_postings[0].positions.encode(encoding_type);
    VarIntEncoder::encodeValue(static_cast<uint32_t>(pos_encoded.size()), encoded);
    encoded.insert(encoded.end(), pos_encoded.begin(), pos_encoded.end());

    // Encode subsequent doc_ids using delta encoding
    for (size_t i = 1; i < sorted_postings.size(); ++i) {
        const uint32_t delta = sorted_postings[i].doc_id - sorted_postings[i - 1].doc_id;
        VarIntEncoder::encodeValue(delta, encoded);

        // Encode position list
        auto pos_data = sorted_postings[i].positions.encode(encoding_type);
        VarIntEncoder::encodeValue(static_cast<uint32_t>(pos_data.size()), encoded);
        encoded.insert(encoded.end(), pos_data.begin(), pos_data.end());
    }

    return encoded;
}

void PostingList::decode(const std::vector<uint8_t>& encoded_data) {
    postings_.clear();
    doc_to_index_.clear();

    if (encoded_data.empty()) {
        return;
    }

    const uint8_t* ptr = encoded_data.data();

    // Decode number of postings
    const uint32_t num_postings = VarIntEncoder::decodeValue(&ptr);

    if (num_postings == 0) {
        return;
    }

    // Decode first doc_id
    uint32_t current_doc_id = VarIntEncoder::decodeValue(&ptr);

    // Decode first position list
    uint32_t pos_size = VarIntEncoder::decodeValue(&ptr);
    std::vector<uint8_t> pos_data(ptr, ptr + pos_size);
    ptr += pos_size;

    postings_.emplace_back(current_doc_id);
    // Note: EncodingType is not stored anymore, we'll detect it from the data format
    postings_.back().positions.decode(pos_data, EncodingType::ADAPTIVE);
    doc_to_index_[current_doc_id] = 0;

    // Decode subsequent postings
    for (uint32_t i = 1; i < num_postings; ++i) {
        const uint32_t delta = VarIntEncoder::decodeValue(&ptr);
        current_doc_id += delta;

        pos_size = VarIntEncoder::decodeValue(&ptr);
        pos_data = std::vector<uint8_t>(ptr, ptr + pos_size);
        ptr += pos_size;

        postings_.emplace_back(current_doc_id);
        postings_.back().positions.decode(pos_data, EncodingType::ADAPTIVE);
        doc_to_index_[current_doc_id] = postings_.size() - 1;
    }
}