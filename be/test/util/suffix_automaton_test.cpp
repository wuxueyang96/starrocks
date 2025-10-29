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

#include "util/suffix_automaton.h"

#include <gtest/gtest.h>

#include "gen_cpp/suffix_automaton.pb.h"
#include "util/slice.h"

namespace starrocks {

TEST(SuffixAutomatonTest, basicTest) {
    SuffixAutomaton sam;

    const std::vector<std::string> strs = {
        "apple",
        "application",
        "banana",
        "apply",
        "pineapple",
        "测试下中文",
        "再来试一下中文"
    };

    for (int i = 0; i < strs.size(); i++) {
        Slice slice(strs[i]);
        sam.extend(&slice, i);
    }
    sam.build_parent_tree();

    const std::vector<std::string> queries = {"app", "ple", "nea", "ban", "xyz", "ply", "中文"};
    const std::vector<std::vector<int>> expect_res = {
        {0, 1, 3, 4},
        {0, 4},
        {4},
        {2},
        {},
        {3},
        {5, 6}
    };

    for (int i = 0; i < queries.size(); ++i) {
        roaring::Roaring roaring;
        Slice slice(queries[i]);
        auto res = sam.query(&slice, &roaring);
        ASSERT_TRUE(res.ok());
        ASSERT_EQ(roaring.cardinality(), expect_res[i].size());
        for (int j = 0; j < expect_res[i].size(); ++j) {
            ASSERT_TRUE(roaring.contains(expect_res[i][j]));
        }
    }
}

TEST(SuffixAutomatonTest, serialize_and_deserialize) {
    const std::vector<std::string> strs = {
        "apple",
        "application",
        "banana",
        "apply",
        "pineapple",
        "测试下中文",
        "再来试一下中文"
    };

    SuffixAutomatonPB suffix_automaton_pb;

    SuffixAutomaton sam1;
    for (int i = 0; i < strs.size(); i++) {
        Slice slice(strs[i]);
        sam1.extend(&slice, i);
    }
    sam1.build_parent_tree();
    sam1.persist(&suffix_automaton_pb);

    SuffixAutomaton sam2;
    sam2.load(suffix_automaton_pb);
    ASSERT_EQ(sam1, sam2);
}

}