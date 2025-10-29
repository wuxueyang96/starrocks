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

#include "suffix_automaton.h"

#include "faststring.h"
#include "gen_cpp/suffix_automaton.pb.h"
#include "slice.h"

namespace starrocks {

int64_t SuffixAutomatonState::memory_usage() const {
    return 4 + 4 + next.size() * 8 + occ.getSizeInBytes(false);
}

Status SuffixAutomatonState::persist(SuffixAutomatonStatePB* suffix_automaton_state_pb) {
    suffix_automaton_state_pb->set_len(len);
    suffix_automaton_state_pb->set_link(link);
    for (const auto& [c, v] : next) {
        suffix_automaton_state_pb->mutable_next()->insert({c, v});
    }
    occ.runOptimize();
    std::string buf;
    buf.reserve(occ.getSizeInBytes(false));
    occ.write(buf.data(), false);
    suffix_automaton_state_pb->set_occ(std::move(buf));
    return Status::OK();
}

Status SuffixAutomatonState::load(const SuffixAutomatonStatePB& suffix_automaton_state_pb) {
    len = suffix_automaton_state_pb.len();
    link = suffix_automaton_state_pb.link();
    for (const auto& [c, v] : suffix_automaton_state_pb.next()) {
        next.insert({c, v});
    }
    occ.read(suffix_automaton_state_pb.occ().data(), false);
    return Status::OK();
}

bool SuffixAutomatonState::operator==(const SuffixAutomatonState& o) const {
    if (len != o.len || link != o.link || next != o.next || occ != o.occ) {
        return false;
    }
    return true;
}

SuffixAutomaton::SuffixAutomaton() {
    _states.emplace_back();
    _last = 0;
}

SuffixAutomaton::~SuffixAutomaton() = default;

Status SuffixAutomaton::persist(SuffixAutomatonPB* suffix_automaton_pb) {
    suffix_automaton_pb->set_last(_last);
    for (auto& _state: _states) {
        RETURN_IF_ERROR(_state.persist(suffix_automaton_pb->add_states()));
    }
    return Status::OK();
}

Status SuffixAutomaton::load(const SuffixAutomatonPB& suffix_automaton_pb) {
    _last = suffix_automaton_pb.last();
    _states.clear();
    _states.reserve(suffix_automaton_pb.states_size());
    for (const auto& suffix_automaton_state_pb : suffix_automaton_pb.states()) {
        SuffixAutomatonState state;
        state.load(suffix_automaton_state_pb);
        _states.emplace_back(std::move(state));
    }
    return Status::OK();
}

int64_t SuffixAutomaton::memory_usage() const {
    int64_t total_memory_usage = 0;
    for (const auto& state : _states) {
        total_memory_usage += state.memory_usage();
    }
    return 4 + total_memory_usage;
}

Status SuffixAutomaton::extend(const Slice* slice, const int32_t string_idx) {
    _last = 0;
    const auto* d = reinterpret_cast<uint8_t*>(slice->data);
    for (int i = 0; i < slice->size; ++i) {
        RETURN_IF_ERROR(_extend(d[i], string_idx));
    }
    return Status::OK();
}

Status SuffixAutomaton::_extend(const uint8_t c, const int32_t string_idx) {
    if (_last >= _states.size()) {
        return Status::InternalError("Invalid state while building SuffixAutomaton");
    }

    if (_states[_last].next.contains(c)) {
        int32_t p = _last;
        const int32_t q = _states[p].next[c];
        if (_states[p].len + 1 == _states[q].len) {
            _states[q].occ.add(string_idx); // 更新出现的字符串
            _last = q;
        } else {
            // 需要分裂节点
            const int32_t clone = _states.size();
            _states.emplace_back(_states[q]);
            _states[clone].len = _states[p].len + 1;
            _states[clone].occ = _states[q].occ;

            // 更新原状态的 link
            const int32_t r = clone;
            while (p != -1 && _states[p].next[c] == q) {
                _states[p].next[c] = r;
                p = _states[p].link;
            }
            _states[q].link = r;
            _states[r].occ.add(string_idx); // 新状态也属于当前字符串
            _last = r;
        }
        return Status::OK();
    }

    const int32_t cur = _states.size();
    _states.emplace_back();
    _states[cur].len = _states[_last].len + 1;
    _states[cur].occ.add(string_idx);  // 当前字符串拥有这个状态

    int32_t p = _last;
    while (p != -1 && !_states[p].next.contains(c)) {
        _states[p].next[c] = cur;
        p = _states[p].link;
    }

    if (p == -1) {
        _states[cur].link = 0;
    } else {
        const int q = _states[p].next[c];
        if (_states[p].len + 1 == _states[q].len) {
            _states[cur].link = q;
        } else {
            const int clone = _states.size();
            _states.push_back(_states[q]);
            _states[clone].len = _states[p].len + 1;
            _states[clone].occ = _states[q].occ;  // 继承

            while (p != -1 && _states[p].next[c] == q) {
                _states[p].next[c] = clone;
                p = _states[p].link;
            }
            _states[q].link = clone;
            _states[cur].link = clone;
        }
    }
    _last = cur;
    return Status::OK();
}

Status SuffixAutomaton::build_parent_tree() {
    std::vector<std::vector<int32_t>> inv_link(_states.size());
    for (int i = 1; i < _states.size(); i++) {
        inv_link[_states[i].link].emplace_back(i);
    }

    // DFS 后序遍历，自底向上合并 occ
    std::function<void(int)> dfs = [&](const int32_t& u) {
        for (const int32_t& v : inv_link[u]) {
            dfs(v);
            _states[u].occ |= _states[v].occ;
        }
    };
    dfs(0);
    return Status::OK();
}

Status SuffixAutomaton::query(const Slice* slice, roaring::Roaring* roaring) {
    if (slice == nullptr) {
        return Status::OK();
    }

    int u = 0;
    for (int i = 0; i < slice->size; ++i) {
        uint8_t val = slice->data[i];
        if (!_states[u].next.contains(val)) {
            return Status::OK();  // 不是任何串的子串
        }
        u = _states[u].next[val];
    }
    *roaring = _states[u].occ;
    return Status::OK();
}

bool SuffixAutomaton::operator==(const SuffixAutomaton& o) const {
    if (_last != o._last || _states.size() != o._states.size()) {
        return false;
    }
    for (int i = 0; i < _states.size(); i++) {
        if (_states[i] != o._states[i]) {
            return false;
        }
    }
    return true;
}

}
