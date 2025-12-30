#include "posting_list.h"

#include <algorithm>
#include <roaring/roaring.hh>

#include "encoder_factory.h"
#include "varint_encoder.h"

// ==================== PostingList Implementation ====================

PostingList::PostingList() = default;

PostingList::~PostingList() = default;

void PostingList::addPosting(uint32_t doc_id, const uint32_t position) {
    if (_doc_ids.empty() || _doc_ids.back() != doc_id) {
        _doc_ids.emplace_back(doc_id);
        _positions.emplace_back(position);
    } else {
        _positions.back().add(position);
    }
}

const std::vector<BitmapUpdateContextRefOrSingleValue>& PostingList::getPositions() const {
    return _positions;
}

size_t PostingList::getDocFrequency() const {
    return _doc_ids.size();
}

std::vector<uint8_t> PostingList::encode(EncodingType encoding_type) const {
    std::vector<uint8_t> encoded;
    if (_doc_ids.empty()) {
        return encoded;
    }

    auto encoder = EncoderFactory::createEncoder(encoding_type);
    for (const auto& position : _positions) {
        if (position.is_context()) {
            auto& mutable_pos = const_cast<BitmapUpdateContextRefOrSingleValue&>(position);
            mutable_pos.flush_pending_adds();
            encoder->encode(*position.roaring(), &encoded);
        } else {
            encoder->encode(position.value(), &encoded);
        }
    }
    return encoded;
}
