#pragma once

#include <string>
#include <vector>

#include "roaring/roaring.hh"
#include "roaring/roaring64map.hh"

class BitmapInvertedIndex {
public:
    BitmapInvertedIndex();
    ~BitmapInvertedIndex();

    // Add a term occurrence (term, doc_id, position)
    void addTerm(const std::string& term, uint32_t doc_id, uint32_t position);

    // Get posting list for a term
    const roaring::Roaring64Map& getPostingList(const std::string& term) const;

    // Get all terms
    std::vector<std::string> getTerms() const;

    // Get number of unique terms
    size_t getTermCount() const;

    // Get total number of postings
    size_t getTotalPostings() const;

    // Save index to disk
    bool saveToDisk(const std::string& file_path) const;

    // Load index from disk
    bool loadFromDisk(const std::string& file_path);

    // Get index statistics
    void printStatistics() const;

    // Clear the index
    void clear();
private:
    std::unordered_map<std::string, roaring::Roaring> _dict_to_docs;
    std::unordered_map<std::string, roaring::Roaring64Map> _index;
};