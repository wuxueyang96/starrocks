#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "compression_util.h"
#include "encoder.h"
#include "posting_list.h"

// InvertedIndex: maps terms to posting lists
class InvertedIndex {
public:
    InvertedIndex();
    ~InvertedIndex();

    // Add a term occurrence (term, doc_id, position)
    void addTerm(const std::string& term, uint32_t doc_id, uint32_t position);

    // Get posting list for a term
    const PostingList* getPostingList(const std::string& term) const;

    // Get all terms
    std::vector<std::string> getTerms() const;

    // Get number of unique terms
    size_t getTermCount() const;

    // Get total number of postings
    size_t getTotalPostings() const;

    // Save index to disk with optional compression
    // encoding_type can be VARINT, FOR_VARINT, PFOR_DELTA, or ADAPTIVE
    // In ADAPTIVE mode, each posting list chooses its own best encoding
    bool saveToDisk(const std::string& file_path, CompressionType compression = CompressionType::NONE,
                    EncodingType encoding = EncodingType::ADAPTIVE) const;

    // Load index from disk
    bool loadFromDisk(const std::string& file_path);

    // Get index statistics
    void printStatistics() const;

    // Clear the index
    void clear();

private:
    std::unordered_map<std::string, PostingList> index_;
};
