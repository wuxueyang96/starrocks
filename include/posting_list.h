#pragma once

#include <vector>
#include <unordered_map>

#include "position_list.h"
#include "encoding_util.h"

// Posting: document ID + position list
struct Posting {
    uint32_t doc_id;
    PositionList positions;

    Posting() : doc_id(0) {}
    explicit Posting(const uint32_t id) : doc_id(id) {}
};

// PostingList: list of postings for a term
class PostingList {
public:
    PostingList();
    ~PostingList();

    // Add a posting (doc_id, position)
    void addPosting(uint32_t doc_id, uint32_t position);

    // Get all postings
    const std::vector<Posting>& getPostings() const;

    // Get number of documents containing this term
    size_t getDocFrequency() const;

    // Encode the posting list using delta encoding and variable-length integers
    std::vector<uint8_t> encode(EncodingType encoding_type = EncodingType::FOR_VARINT) const;

    // Decode from compressed bytes
    void decode(const std::vector<uint8_t>& encoded_data, EncodingType encoding_type = EncodingType::FOR_VARINT);

private:
    std::vector<Posting> postings_;
    std::unordered_map<uint32_t, size_t> doc_to_index_;  // Map doc_id to index in postings_
};