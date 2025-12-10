#include "inverted_index.h"

#include <iostream>
#include <fstream>
#include <algorithm>
#include <stdexcept>

// ==================== InvertedIndex Implementation ====================

InvertedIndex::InvertedIndex() = default;

InvertedIndex::~InvertedIndex() = default;

void InvertedIndex::addTerm(const std::string& term, const uint32_t doc_id, const uint32_t position) {
    index_[term].addPosting(doc_id, position);
}

const PostingList* InvertedIndex::getPostingList(const std::string& term) const {
    const auto it = index_.find(term);
    if (it != index_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<std::string> InvertedIndex::getTerms() const {
    std::vector<std::string> terms;
    terms.reserve(index_.size());
    for (const auto&[term, _] : index_) {
        terms.push_back(term);
    }
    return terms;
}

size_t InvertedIndex::getTermCount() const {
    return index_.size();
}

size_t InvertedIndex::getTotalPostings() const {
    size_t total = 0;
    for (const auto&[_, posting] : index_) {
        total += posting.getDocFrequency();
    }
    return total;
}

bool InvertedIndex::saveToDisk(const std::string& file_path) const {
    std::ofstream file(file_path, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open file for writing: " << file_path << std::endl;
        return false;
    }

    // Write number of terms
    const auto num_terms = static_cast<uint32_t>(index_.size());
    file.write(reinterpret_cast<const char*>(&num_terms), sizeof(num_terms));

    // Write each term and its posting list
    for (const auto&[term, posting_list] : index_) {
        // Write term length and term
        const auto term_length = static_cast<uint32_t>(term.size());
        file.write(reinterpret_cast<const char*>(&term_length), sizeof(term_length));
        file.write(term.c_str(), term_length);

        // Encode and write posting list
        std::vector<uint8_t> encoded = posting_list.encode();
        const auto encoded_size = static_cast<uint32_t>(encoded.size());
        file.write(reinterpret_cast<const char*>(&encoded_size), sizeof(encoded_size));
        file.write(reinterpret_cast<const char*>(encoded.data()), encoded_size);
    }

    file.close();
    return true;
}

bool InvertedIndex::loadFromDisk(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open file for reading: " << file_path << std::endl;
        return false;
    }

    clear();

    // Read number of terms
    uint32_t num_terms;
    file.read(reinterpret_cast<char*>(&num_terms), sizeof(num_terms));

    // Read each term and its posting list
    for (uint32_t i = 0; i < num_terms; ++i) {
        // Read term length and term
        uint32_t term_length;
        file.read(reinterpret_cast<char*>(&term_length), sizeof(term_length));

        std::string term(term_length, '\0');
        file.read(&term[0], term_length);

        // Read encoded posting list
        uint32_t encoded_size;
        file.read(reinterpret_cast<char*>(&encoded_size), sizeof(encoded_size));

        std::vector<uint8_t> encoded(encoded_size);
        file.read(reinterpret_cast<char*>(encoded.data()), encoded_size);

        // Decode posting list
        index_[term].decode(encoded);
    }

    file.close();
    return true;
}

void InvertedIndex::printStatistics() const {
    std::cout << "\n=== Inverted Index Statistics ===" << std::endl;
    std::cout << "Number of unique terms: " << getTermCount() << std::endl;
    std::cout << "Total postings: " << getTotalPostings() << std::endl;

    // Calculate average positions per posting
    size_t total_positions = 0;
    for (const auto&[_, posting_list] : index_) {
        for (const auto& posting : posting_list.getPostings()) {
            total_positions += posting.positions.size();
        }
    }

    if (getTotalPostings() > 0) {
        std::cout << "Average positions per posting: "
                  << static_cast<double>(total_positions) / getTotalPostings() << std::endl;
    }

    // Show top 10 most frequent terms
    std::vector<std::pair<std::string, size_t>> term_freqs;
    term_freqs.reserve(index_.size());

    for (const auto&[term, postings] : index_) {
        term_freqs.emplace_back(term, postings.getDocFrequency());
    }
    std::sort(term_freqs.begin(), term_freqs.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    std::cout << "\nTop 10 most frequent terms:" << std::endl;
    for (size_t i = 0; i < std::min(static_cast<size_t>(10), term_freqs.size()); ++i) {
        std::cout << "  " << term_freqs[i].first << ": " << term_freqs[i].second << " docs" << std::endl;
    }
}

void InvertedIndex::clear() {
    index_.clear();
}
