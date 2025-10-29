// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <common/status.h>

#include <roaring/roaring.hh>
#include <unordered_map>

namespace starrocks {

class Slice;
class SuffixAutomatonPB;
class SuffixAutomatonStatePB;

struct SuffixAutomatonState {
    int32_t len{0};
    int32_t link{-1};
    std::unordered_map<uint8_t, int32_t> next{};
    roaring::Roaring occ{};

    int64_t memory_usage() const;

    Status persist(SuffixAutomatonStatePB* suffix_automaton_state_pb);
    Status load(const SuffixAutomatonStatePB& suffix_automaton_state_pb);

    // used for test
    bool operator==(const SuffixAutomatonState& o) const;
};

class SuffixAutomaton {
public:
    explicit SuffixAutomaton();
    ~SuffixAutomaton();

    Status persist(SuffixAutomatonPB* suffix_automaton_pb);
    Status load(const SuffixAutomatonPB& suffix_automaton_pb);
    int64_t memory_usage() const;

    Status extend(const Slice* slice, int32_t string_idx);
    Status build_parent_tree();

    Status query(const Slice* slice, roaring::Roaring* roaring);

    // used for test
    bool operator==(const SuffixAutomaton&) const;
private:
    Status _extend(uint8_t c, int32_t string_idx);

    std::vector<SuffixAutomatonState> _states;
    int32_t _last;
};

}
