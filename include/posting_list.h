#pragma once

#include <vector>

#include "bitmap_update_context.h"
#include "encoder.h"

// PostingList: list of postings for a term
class PostingList {
public:
    PostingList();
    ~PostingList();

    // Add a posting (doc_id, position)
    void addPosting(uint32_t doc_id, uint32_t position);

    // Get all postings
    const std::vector<BitmapUpdateContextRefOrSingleValue>& getPositions() const;

    // Get number of documents containing this term
    size_t getDocFrequency() const;

    // Encode the posting list using delta encoding and variable-length integers
    // In ADAPTIVE mode, each position list can use different encoding
    std::vector<uint8_t> encode(EncodingType encoding_type) const;

private:
    std::vector<uint32_t> _doc_ids;
    std::vector<BitmapUpdateContextRefOrSingleValue> _positions;
};