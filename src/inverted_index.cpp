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

bool InvertedIndex::saveToDisk(const std::string& file_path, CompressionType compression, EncodingType encoding) const {
    std::ofstream file(file_path, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open file for writing: " << file_path << std::endl;
        return false;
    }

    // Write magic number for file format identification
    const uint32_t magic = 0x49444558; // "IDEX" in hex
    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));

    // Write version
    const uint32_t version = 1;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    // Write compression type
    const uint8_t comp_type = static_cast<uint8_t>(compression);
    file.write(reinterpret_cast<const char*>(&comp_type), sizeof(comp_type));

    // Write encoding type
    const uint8_t enc_type = static_cast<uint8_t>(encoding);
    file.write(reinterpret_cast<const char*>(&enc_type), sizeof(enc_type));

    // Write number of terms
    const auto num_terms = static_cast<uint32_t>(index_.size());
    file.write(reinterpret_cast<const char*>(&num_terms), sizeof(num_terms));

    // Write each term and its posting list
    for (const auto& [term, posting_list] : index_) {
        // Write term length and term
        const auto term_length = static_cast<uint32_t>(term.size());
        file.write(reinterpret_cast<const char*>(&term_length), sizeof(term_length));
        file.write(term.c_str(), term_length);

        // Encode posting list
        std::vector<uint8_t> encoded = posting_list.encode(encoding);
        const auto uncompressed_size = static_cast<uint32_t>(encoded.size());

        // Apply compression if specified
        std::vector<uint8_t> final_data;
        if (compression != CompressionType::NONE) {
            final_data = CompressionUtil::compress(encoded, compression);
        } else {
            final_data = encoded;
        }

        // Write uncompressed size (needed for decompression)
        file.write(reinterpret_cast<const char*>(&uncompressed_size), sizeof(uncompressed_size));

        // Write compressed/encoded size and data
        const auto final_size = static_cast<uint32_t>(final_data.size());
        file.write(reinterpret_cast<const char*>(&final_size), sizeof(final_size));
        file.write(reinterpret_cast<const char*>(final_data.data()), final_size);
    }

    file.close();

    std::cout << "Index saved with compression: " << CompressionUtil::compressionTypeToString(compression)
              << ", encoding: " << get_encoding_type_name(encoding) << std::endl;
    return true;
}

bool InvertedIndex::loadFromDisk(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open file for reading: " << file_path << std::endl;
        return false;
    }

    clear();

    // Read magic number
    uint32_t magic;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != 0x49444558) {
        std::cerr << "Error: Invalid file format" << std::endl;
        return false;
    }

    // Read version
    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != 1) {
        std::cerr << "Error: Unsupported file version: " << version << std::endl;
        return false;
    }

    // Read compression type
    uint8_t comp_type;
    file.read(reinterpret_cast<char*>(&comp_type), sizeof(comp_type));
    const CompressionType compression = static_cast<CompressionType>(comp_type);

    // Read encoding type
    uint8_t enc_type;
    file.read(reinterpret_cast<char*>(&enc_type), sizeof(enc_type));
    const EncodingType encoding = static_cast<EncodingType>(enc_type);

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

        // Read uncompressed size
        uint32_t uncompressed_size;
        file.read(reinterpret_cast<char*>(&uncompressed_size), sizeof(uncompressed_size));

        // Read compressed/encoded size
        uint32_t encoded_size;
        file.read(reinterpret_cast<char*>(&encoded_size), sizeof(encoded_size));

        // Read compressed/encoded data
        std::vector<uint8_t> compressed_data(encoded_size);
        file.read(reinterpret_cast<char*>(compressed_data.data()), encoded_size);

        // Decompress if needed
        std::vector<uint8_t> encoded_data;
        if (compression != CompressionType::NONE) {
            encoded_data = CompressionUtil::decompress(compressed_data, compression, uncompressed_size);
        } else {
            encoded_data = compressed_data;
        }

        // Decode posting list (encoding type is stored in the data)
        index_[term].decode(encoded_data);
    }

    file.close();

    std::string encoding_name;
    switch (encoding) {
    case EncodingType::VARINT:
        encoding_name = "VarInt";
        break;
    case EncodingType::FOR_VARINT:
        encoding_name = "FOR+VarInt";
        break;
    case EncodingType::PFOR_DELTA:
        encoding_name = "PForDelta";
        break;
    case EncodingType::ADAPTIVE:
        encoding_name = "Adaptive (Mixed)";
        break;
    default:
        encoding_name = "Unknown";
        break;
    }

    std::cout << "Index loaded with compression: " << CompressionUtil::compressionTypeToString(compression)
              << ", encoding: " << encoding_name << std::endl;
    return true;
}

void InvertedIndex::printStatistics() const {
    std::cout << "\n=== Inverted Index Statistics ===" << std::endl;
    std::cout << "Number of unique terms: " << getTermCount() << std::endl;
    std::cout << "Total postings: " << getTotalPostings() << std::endl;

    // Calculate average positions per posting
    size_t total_positions = 0;
    for (const auto& [_, posting_list] : index_) {
        for (const auto& posting : posting_list.getPostings()) {
            total_positions += posting.positions.size();
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
