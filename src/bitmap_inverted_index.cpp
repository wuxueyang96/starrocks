#include "bitmap_inverted_index.h"

#include <fstream>
#include <iostream>

#include "posting_list.h"

BitmapInvertedIndex::BitmapInvertedIndex() = default;

BitmapInvertedIndex::~BitmapInvertedIndex() = default;

void BitmapInvertedIndex::addTerm(const std::string& term, uint32_t doc_id, uint32_t position) {
    const uint64_t val = static_cast<uint64_t>(doc_id) << 32 | static_cast<uint64_t>(position);
    _index[term].add(val);
    _dict_to_docs[term].add(doc_id);
}

const roaring::Roaring64Map& BitmapInvertedIndex::getPostingList(const std::string& term) const {
    if (_index.find(term) == _index.end()) {
        return roaring::Roaring64Map();
    }
    return _index.at(term);
}

std::vector<std::string> BitmapInvertedIndex::getTerms() const {
    std::vector<std::string> terms;
    terms.reserve(_index.size());
    for (const auto& [dict, _] : _index) {
        terms.emplace_back(dict);
    }
    return terms;
}

size_t BitmapInvertedIndex::getTermCount() const {
    return _index.size();
}

size_t BitmapInvertedIndex::getTotalPostings() const {
    size_t total = 0;
    for (const auto& [_, posting_list] : _index) {
        total += posting_list.cardinality();
    }
    return total;
}

bool BitmapInvertedIndex::saveToDisk(const std::string& file_path) const {
    std::ofstream file(file_path, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open file for writing: " << file_path << std::endl;
        return false;
    }

    const auto num_terms = static_cast<uint32_t>(_index.size());
    file.write(reinterpret_cast<const char*>(&num_terms), sizeof(num_terms));

    for (auto it = _index.begin(); it != _index.end(); ++it) {
        auto term = it->first;
        auto posting_list = it->second;

        // Write term length and term
        const auto term_length = static_cast<uint32_t>(term.size());
        file.write(reinterpret_cast<const char*>(&term_length), sizeof(term_length));
        file.write(term.c_str(), term_length);

        posting_list.runOptimize();

        size_t size = posting_list.getSizeInBytes(false);

        std::string buf(size, '\0');

        posting_list.write(buf.data(), false);

        file.write(reinterpret_cast<const char*>(&size), sizeof(size));

        const char* ptr = buf.data();
        size_t remaining = size;
        size_t offset = 0;
        while (remaining > 0) {
            const uint32_t byte_to_write =
                    std::min(remaining, static_cast<size_t>(std::numeric_limits<int32_t>::max()));
            file.write(ptr + offset, byte_to_write);
            remaining -= byte_to_write;
            offset += byte_to_write;
        }
    }

    file.close();
    return true;
}

bool BitmapInvertedIndex::loadFromDisk(const std::string& file_path) {
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
        file.read(term.data(), term_length);

        // Read encoded posting list
        size_t encoded_size;
        file.read(reinterpret_cast<char*>(&encoded_size), sizeof(encoded_size));

        std::string encoded;
        encoded.resize(encoded_size);

        char* ptr = encoded.data();
        size_t offset = 0;
        while (encoded_size > 0) {
            const uint32_t byte_to_read =
                    std::min(encoded_size, static_cast<size_t>(std::numeric_limits<int32_t>::max()));
            file.read(ptr + offset, sizeof(byte_to_read));
            encoded_size -= byte_to_read;
            offset += byte_to_read;
        }

        roaring::Roaring64Map roaring = roaring::Roaring64Map::read(encoded.data(), false);
        // Decode posting list
        _index[term] = roaring;
    }

    file.close();
    return true;
}

void BitmapInvertedIndex::printStatistics() const {
    std::cout << "\n=== Inverted Index Statistics ===" << std::endl;
    std::cout << "Number of unique terms: " << getTermCount() << std::endl;
    std::cout << "Total postings: " << getTotalPostings() << std::endl;

    // Calculate average positions per posting
    size_t total_positions = 0;
    for (const auto& [_, posting_list] : _index) {
        total_positions += posting_list.cardinality();
    }

    if (getTotalPostings() > 0) {
        std::cout << "Average positions per posting: " << static_cast<double>(total_positions) / getTotalPostings()
                  << std::endl;
    }

    // Show top 10 most frequent terms
    std::vector<std::pair<std::string, size_t>> term_freqs;
    term_freqs.reserve(_dict_to_docs.size());

    for (const auto& [term, doc_ids] : _dict_to_docs) {
        term_freqs.emplace_back(term, doc_ids.cardinality());
    }
    std::sort(term_freqs.begin(), term_freqs.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

    std::cout << "\nTop 10 most frequent terms:" << std::endl;
    for (size_t i = 0; i < std::min(static_cast<size_t>(10), term_freqs.size()); ++i) {
        std::cout << "  " << term_freqs[i].first << ": " << term_freqs[i].second << " docs" << std::endl;
    }
}

void BitmapInvertedIndex::clear() {
    _index.clear();
    _dict_to_docs.clear();
}
