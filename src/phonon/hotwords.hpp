#pragma once
// Hotwords for Phonon-2's TDT decoder, ported from Fermion's hotwords.py: the
// words become an Aho-Corasick automaton over the vocabulary pieces that can
// spell them, and the decoder adds `lambda` to a piece's score whenever it
// advances a listed word (a bonus only; blank is never biased).

#include <cstdint>
#include <string>
#include <vector>

namespace tala::phonon {

class Hotwords {
public:
    static constexpr std::size_t kMaxWords = 25;
    static constexpr float kDefaultLambda = 2.0f;

    // `vocabulary`: the model's pieces, "▁" marking a word start.
    Hotwords(const std::vector<std::string>& words, const std::vector<std::string>& vocabulary,
             float lambda = kDefaultLambda);

    bool empty() const { return states_ <= 1; }

    // The tables phonon2_tdt_set_bias takes.
    int states() const { return states_; }
    int columns() const { return int(toks_.size()); }
    const std::int32_t* table() const { return table_.data(); }
    const std::int32_t* toks() const { return toks_.data(); }
    const std::int32_t* col() const { return col_.data(); }
    int colSize() const { return int(col_.size()); }
    const std::int32_t* depth() const { return depth_.data(); }
    float lambda() const { return lambda_; }

private:
    int states_ = 1;
    float lambda_;
    std::vector<std::int32_t> table_, toks_, col_, depth_;
};

} // namespace tala::phonon
