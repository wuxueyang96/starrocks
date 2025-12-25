#include <iostream>
#include <roaring/roaring.hh>
#include <sstream>
#include <string>

// std::vector<std::vector<std::string>> rows = {{"hello", "world", "test"},
//                                               {"test", "write", "position"},
//                                               {"position", "write", "with"},
//                                               {"hello", "test", "world", "write", "position", "with"}};
// hello
// position
// test
// with
// world
// write

std::vector<roaring::Roaring> read_positions(const std::vector<uint32_t>& offsets,
                                             const std::vector<roaring::Roaring>& positions, uint32_t dict_id,
                                             const std::vector<uint64_t>& ranks) {
    uint32_t offset = offsets[dict_id];
    std::vector<roaring::Roaring> result;
    result.reserve(ranks.size());

    for (size_t i = 0; i < ranks.size(); ++i) {
        uint64_t doc_rank = ranks[i];
        const uint32_t ordinal = offset + doc_rank - 1;
        result.emplace_back(positions[ordinal]);
    }
    return result;
}

bool _phrase_query(const std::string& search_query, roaring::Roaring* bit_map) {
    std::istringstream iss(search_query);

    // row_id -> dict_id -> positions
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, roaring::Roaring>> positions;

    std::unordered_map<std::string, uint32_t> dicts = {{"hello", 0}, {"position", 1}, {"test", 2},
                                                       {"with", 3},  {"world", 4},    {"write", 5}};
    std::vector inverted_index = {roaring::Roaring::bitmapOfList({0, 3}),    roaring::Roaring::bitmapOfList({1, 2, 3}),
                                  roaring::Roaring::bitmapOfList({0, 1, 3}), roaring::Roaring::bitmapOfList({2, 3}),
                                  roaring::Roaring::bitmapOfList({0, 3}),    roaring::Roaring::bitmapOfList({1, 2, 3})};
    std::vector postings = {
            // for dict id 0
            roaring::Roaring::bitmapOfList({0}), // row 0
            roaring::Roaring::bitmapOfList({0}), // row 3
            // for dict id 1
            roaring::Roaring::bitmapOfList({2}), // row 1
            roaring::Roaring::bitmapOfList({0}), // row 2
            roaring::Roaring::bitmapOfList({4}), // row 3
            // for dict id 2
            roaring::Roaring::bitmapOfList({2}), // row 0
            roaring::Roaring::bitmapOfList({0}), // row 1
            roaring::Roaring::bitmapOfList({1}), // row 3
            // for dict id 3
            roaring::Roaring::bitmapOfList({2}), // row 2
            roaring::Roaring::bitmapOfList({5}), // row 3
            // for dict id 4
            roaring::Roaring::bitmapOfList({1}), // row 0
            roaring::Roaring::bitmapOfList({2}), // row 3
            // for dict id 6
            roaring::Roaring::bitmapOfList({1}), // row 1
            roaring::Roaring::bitmapOfList({1}), // row 2
            roaring::Roaring::bitmapOfList({3}), // row 3
    };
    std::vector<uint32_t> offsets = {0, 2, 5, 8, 10, 12, 14};

    roaring::Roaring filtered_rows;
    std::vector<uint32_t> dict_ids;
    std::vector<roaring::Roaring> full_doc_ids;
    filtered_rows.addRange(0, dicts.size());

    std::string cur_predicate;
    while (iss >> cur_predicate) {
        std::cout << "filter " << cur_predicate << std::endl;
        if (!dicts.contains(cur_predicate)) {
            bit_map->clear();
            return false;
        }

        uint32_t dict_id = dicts[cur_predicate];
        const roaring::Roaring& doc_ids = inverted_index[dict_id];

        if (doc_ids.cardinality() <= 0) {
            std::cout << "no available doc ids found for " << cur_predicate << std::endl;
            bit_map->clear();
            return false;
        }

        filtered_rows &= doc_ids;
        if (filtered_rows.cardinality() <= 0) {
            std::cout << "no available rows found after intersection." << std::endl;
            bit_map->clear();
            return false;
        }

        full_doc_ids.emplace_back(doc_ids);
        dict_ids.emplace_back(dict_id);
    }

    std::cout << "after intersection " << filtered_rows.cardinality() << " rows found as candidate." << std::endl;

    std::vector<uint32_t> candidate_row_ids(filtered_rows.cardinality(), 0);
    std::vector<uint64_t> ranks(filtered_rows.cardinality(), 0);

    filtered_rows.toUint32Array(candidate_row_ids.data());
    for (uint32_t i = 0; i < candidate_row_ids.size(); ++i) {
        std::cout << "row " << candidate_row_ids[i] << " rank : " << ranks[i] << std::endl;
    }

    for (uint32_t i = 0; i < dict_ids.size(); ++i) {
        uint32_t dict_id = dict_ids[i];
        full_doc_ids[i].rank_many(candidate_row_ids.data(), candidate_row_ids.data() + candidate_row_ids.size(),
                                  ranks.data());
        auto ranked_positions = read_positions(offsets, postings, dict_id, ranks);
        for (uint32_t j = 0; j < candidate_row_ids.size(); ++j) {
            uint32_t row_id = candidate_row_ids[j];
            positions[row_id][dict_id] = ranked_positions[j];
        }
    }

    for (const uint32_t& row : candidate_row_ids) {
        for (auto dict_to_position_list = positions.at(row);
             const uint32_t start : dict_to_position_list[dict_ids[0]]) {
            bool found = true;
            for (size_t offset = 1; offset < dict_ids.size(); ++offset) {
                if (const auto& position_list = dict_to_position_list.at(dict_ids[offset]);
                    !position_list.contains(start + offset)) {
                    found = false;
                    break;
                }
            }
            if (found) {
                bit_map->add(row);
                break;
            }
        }
    }
    return true;
}
int main() {
    const std::string& query = "hello world";
    roaring::Roaring roaring;
    if (_phrase_query(query, &roaring)) {
        std::cout << "match " << roaring.cardinality() << " rows." << std::endl;
        for (const auto& row : roaring) {
            std::cout << "match row: " << row << std::endl;
        }
    } else {
        std::cout << "no match" << std::endl;
    }
    return 0;
}