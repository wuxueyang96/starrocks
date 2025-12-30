#include "tokenizer.h"

Tokenizer::Tokenizer(const Config& config) : stop_words(std::move(config.stop_words)) {}

Tokenizer::~Tokenizer() = default;

// Simple tokenizer: split text by whitespace and punctuation
std::vector<std::string> Tokenizer::tokenize(const std::string& text) const {
    std::vector<std::string> tokens;
    std::string current_token;

    for (const char c : text) {
        if (std::isalnum(c)) {
            current_token += std::tolower(c);
        } else if (!current_token.empty()) {
            if (stop_words.count(current_token) == 0) {
                tokens.push_back(current_token);
            }
            current_token.clear();
        }
    }
    if (!current_token.empty() && stop_words.count(current_token) == 0) {
        tokens.push_back(current_token);
    }
    return tokens;
}