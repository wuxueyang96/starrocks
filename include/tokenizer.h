#pragma once

#include <string>
#include <unordered_set>

#include "config.h"

class Tokenizer {
public:
    explicit Tokenizer(const Config& config);
    ~Tokenizer();

    std::vector<std::string> tokenize(const std::string& text) const;
private:
    std::unordered_set<std::string> stop_words;
};