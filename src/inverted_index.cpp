#include "inverted_index.h"

#include <algorithm>
#include <fstream>
#include <iostream>
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
    for (const auto& [term, _] : index_) {
        terms.push_back(term);
    }
    return terms;
}

size_t InvertedIndex::getTermCount() const {
    return index_.size();
}

size_t InvertedIndex::getTotalPostings() const {
    size_t total = 0;
    for (const auto& [_, posting] : index_) {
        total += posting.getDocFrequency();
    }
    return total;
}

std::vector<uint8_t> InvertedIndex::encode(const EncodingType& encoding, const CompressionType& compression) const {
    std::vector<uint8_t> result;

    // Write magic number for file format identification
    constexpr uint32_t magic = 0x49444558; // "IDEX" in hex
    append_uint32(result, magic);

    constexpr uint32_t version = 1;
    append_uint32(result, version);

    const uint8_t comp_type = static_cast<uint8_t>(compression);
    result.push_back(comp_type);

    const uint8_t enc_type = static_cast<uint8_t>(encoding);
    result.push_back(enc_type);

    const auto num_terms = static_cast<uint32_t>(index_.size());
    append_uint32(result, num_terms);

    for (const auto& [term, posting_list] : index_) {
        // Write term length and term
        const auto term_length = static_cast<uint32_t>(term.size());
        append_uint32(result, term_length);
        result.insert(result.end(), term.begin(), term.end());

        // Encode posting list
        std::vector<uint8_t> encoded = posting_list.encode(encoding);

        const auto uncompressed_size = static_cast<uint32_t>(encoded.size());
        append_uint32(result, uncompressed_size);

        // Apply compression if specified
        std::vector<uint8_t> final_data;
        if (compression != CompressionType::NONE) {
            final_data = CompressionUtil::compress(encoded, compression);
        } else {
            final_data = encoded;
        }

        const auto final_size = static_cast<uint32_t>(final_data.size());
        append_uint32(result, final_size);

        result.insert(result.end(), final_data.begin(), final_data.end());
    }
    return result;
}

bool InvertedIndex::saveToDisk(const std::string& file_path, CompressionType compression, EncodingType encoding) const {
    std::ofstream file(file_path, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open file for writing: " << file_path << std::endl;
        return false;
    }

    const auto& result = encode(encoding, compression);
    file.write(reinterpret_cast<const char*>(result.data()), result.size());
    file.close();

    std::cout << "Index saved with compression: " << CompressionUtil::compressionTypeToString(compression)
              << ", encoding: " << get_encoding_type_name(encoding) << std::endl;
    return true;
}

bool InvertedIndex::loadFromDisk(const std::string& file_path) {
    // TODO: Decode functionality has been removed, loadFromDisk is disabled
    std::cerr << "Error: loadFromDisk is not supported (decode functionality removed)" << std::endl;
    return false;
}

void InvertedIndex::printStatistics() const {
    std::cout << "\n=== Inverted Index Statistics ===" << std::endl;
    std::cout << "Number of unique terms: " << getTermCount() << std::endl;
    std::cout << "Total postings: " << getTotalPostings() << std::endl;

    // Calculate average positions per posting
    size_t total_positions = 0;
    for (const auto& [_, posting_list] : index_) {
        for (const auto& posting : posting_list.getPositions()) {
            if (posting.is_context()) {
                total_positions += posting.roaring()->cardinality();
            } else {
                total_positions += 1;
            }
        }
    }

    if (getTotalPostings() > 0) {
        std::cout << "Average positions per posting: " << static_cast<double>(total_positions) / getTotalPostings()
                  << std::endl;
    }

    // Show top 10 most frequent terms
    std::vector<std::pair<std::string, size_t>> term_freqs;
    term_freqs.reserve(index_.size());

    for (const auto& [term, postings] : index_) {
        term_freqs.emplace_back(term, postings.getDocFrequency());
    }
    std::sort(term_freqs.begin(), term_freqs.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

    std::cout << "\nTop 10 most frequent terms:" << std::endl;
    for (size_t i = 0; i < std::min(static_cast<size_t>(10), term_freqs.size()); ++i) {
        std::cout << "  " << term_freqs[i].first << ": " << term_freqs[i].second << " docs" << std::endl;
    }
}

void InvertedIndex::clear() {
    index_.clear();
}

void InvertedIndex::append_uint32(std::vector<uint8_t>& buffer, uint32_t value) {
    buffer.push_back(static_cast<uint8_t>(value & 0xFF));
    buffer.push_back(static_cast<uint8_t>(value >> 8 & 0xFF));
    buffer.push_back(static_cast<uint8_t>(value >> 16 & 0xFF));
    buffer.push_back(static_cast<uint8_t>(value >> 24 & 0xFF));
}

