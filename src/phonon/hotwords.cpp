#include "hotwords.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>

namespace tala::phonon {

namespace {

constexpr std::size_t kMaxSegmentations = 6;
constexpr int kMaxStates = 8192;
const std::string kBoundary = "▁";

bool special(const std::string& p) {
    return (p.starts_with("<|") && p.ends_with("|>")) || p == "<unk>" || p == "<pad>";
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n') {
            if (!cur.empty())
                out.push_back(std::move(cur)), cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty())
        out.push_back(std::move(cur));
    return out;
}

std::string join(const std::vector<std::string>& w, const std::string& sep) {
    std::string out;
    for (std::size_t i = 0; i < w.size(); ++i)
        out += (i ? sep : "") + w[i];
    return out;
}

// ASCII case changes, as Python's lower()/upper() do for the names people add
// to a dictionary; other letters pass through.
std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return s;
}

std::string capitalise(const std::string& phrase) {
    std::vector<std::string> words = split(phrase);
    for (std::string& w : words)
        if (!w.empty() && w[0] >= 'a' && w[0] <= 'z')
            w[0] = char(w[0] - 'a' + 'A');
    return join(words, " ");
}

// As given, all lower, each word capitalised.
std::vector<std::string> casings(const std::string& phrase) {
    std::vector<std::string> out;
    for (const std::string& v : {phrase, lower(phrase), capitalise(phrase)})
        if (std::ranges::find(out, v) == out.end())
            out.push_back(v);
    return out;
}

struct PieceIndex {
    std::unordered_map<std::string, int> ids;
    std::size_t maxLen = 1;
};

// Every way to write `text` as vocabulary pieces, shortest first, at most kMaxSegmentations.
std::vector<std::vector<int>> segmentations(const std::string& text, const PieceIndex& index) {
    const std::size_t n = text.size();
    std::map<std::size_t, std::vector<std::vector<int>>> memo;
    memo[n] = {{}};
    auto rec = [&](auto&& self, std::size_t pos) -> const std::vector<std::vector<int>>& {
        if (auto it = memo.find(pos); it != memo.end())
            return it->second;
        std::vector<std::vector<int>> out;
        const std::size_t limit = 4 * kMaxSegmentations;
        for (std::size_t end = std::min(n, pos + index.maxLen); end > pos && out.size() < limit; --end) {
            auto id = index.ids.find(text.substr(pos, end - pos));
            if (id == index.ids.end())
                continue;
            for (const std::vector<int>& tail : self(self, end)) {
                std::vector<int> seq{id->second};
                seq.insert(seq.end(), tail.begin(), tail.end());
                out.push_back(std::move(seq));
                if (out.size() >= limit)
                    break;
            }
        }
        std::ranges::stable_sort(out, {}, &std::vector<int>::size);
        if (out.size() > kMaxSegmentations)
            out.resize(kMaxSegmentations);
        return memo[pos] = std::move(out);
    };
    return rec(rec, 0);
}

} // namespace

Hotwords::Hotwords(const std::vector<std::string>& words, const std::vector<std::string>& vocabulary, float lambda)
    : lambda_(lambda) {
    PieceIndex index;
    for (std::size_t i = 0; i < vocabulary.size(); ++i)
        if (!special(vocabulary[i]) && !index.ids.contains(vocabulary[i])) {
            index.ids.emplace(vocabulary[i], int(i));
            index.maxLen = std::max(index.maxLen, vocabulary[i].size());
        }

    // Distinct words in order (case-insensitive), at most kMaxWords.
    std::vector<std::string> clean;
    std::set<std::string> seen;
    for (const std::string& w : words) {
        const std::string c = join(split(w), " ");
        if (!c.empty() && seen.insert(lower(c)).second)
            clean.push_back(c);
        if (clean.size() == kMaxWords)
            break;
    }
    std::set<std::vector<int>> phrases;
    for (const std::string& w : clean)
        for (const std::string& v : casings(w))
            for (std::vector<int>& seq : segmentations(kBoundary + join(split(v), kBoundary), index))
                phrases.insert(std::move(seq));

    // The trie, then failure links breadth first.
    std::vector<std::map<int, int>> go(1);
    std::vector<int> depth{0};
    for (const std::vector<int>& seq : phrases) {
        int s = 0;
        for (int t : seq) {
            auto it = go[s].find(t);
            if (it == go[s].end()) {
                go.emplace_back();
                depth.push_back(depth[s] + 1);
                it = go[s].emplace(t, int(go.size()) - 1).first;
            }
            s = it->second;
        }
    }
    const int S = int(go.size());
    if (S > kMaxStates)
        return;  // too large: empty (the unbiased decode)
    std::vector<int> fail(S, 0), order;
    std::deque<int> q;
    for (auto& [t, c] : go[0])
        q.push_back(c);
    while (!q.empty()) {
        const int s = q.front();
        q.pop_front();
        order.push_back(s);
        for (auto& [t, c] : go[s]) {
            int f = fail[s];
            while (f && !go[f].contains(t))
                f = fail[f];
            auto it = go[f].find(t);
            fail[c] = it != go[f].end() && it->second != c ? it->second : 0;
            q.push_back(c);
        }
    }
    std::set<int> pieces;
    for (const auto& g : go)
        for (auto& [t, c] : g)
            pieces.insert(t);
    toks_.assign(pieces.begin(), pieces.end());
    if (toks_.empty())
        toks_.push_back(0);
    const int K = int(toks_.size());
    std::map<int, int> colmap;
    for (int k = 0; k < K; ++k)
        colmap[toks_[k]] = k;
    table_.assign(std::size_t(S) * K, 0);
    for (auto& [t, c] : go[0])
        table_[colmap[t]] = c;
    for (int s : order) {
        std::copy_n(&table_[std::size_t(fail[s]) * K], K, &table_[std::size_t(s) * K]);
        for (auto& [t, c] : go[s])
            table_[std::size_t(s) * K + colmap[t]] = c;
    }
    col_.assign(vocabulary.size() + 1, -1);
    for (auto& [t, k] : colmap)
        if (std::size_t(t) < col_.size())
            col_[std::size_t(t)] = k;
    depth_.assign(depth.begin(), depth.end());
    states_ = S;
}

} // namespace tala::phonon
