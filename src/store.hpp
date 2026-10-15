#pragma once
// Everything tala keeps: settings, past dictations, the dictionary and
// snippets, in one SQLite file ($XDG_DATA_HOME/tala/tala.db). No config
// files: the Hub is where settings change.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace tala {

struct Dictation {
    std::int64_t id = 0;
    std::int64_t time = 0;  // ms since the epoch
    std::string app;        // app id it went to, when known
    std::string raw;        // what the model heard
    std::string text;       // what was pasted
    double seconds = 0;     // audio length
    std::string status;     // "ok", "failed", "cancelled", "empty"
    std::string audio;      // saved recording, "" once pruned
};

struct Word {
    std::int64_t id = 0;
    std::string word;         // the spelling wanted
    std::string misspelling;  // "" for a plain word (a hotword only)
};

struct Snippet {
    std::int64_t id = 0;
    std::string trigger;
    std::string expansion;
};

class Store {
public:
    explicit Store(const std::string& path);
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    bool ok() const { return db_ != nullptr; }

    std::optional<std::string> setting(const std::string& key) const;
    std::string setting(const std::string& key, const std::string& fallback) const;
    bool flag(const std::string& key, bool fallback) const;
    void set(const std::string& key, const std::string& value);

    std::int64_t add(const Dictation& d);
    void update(const Dictation& d);
    std::optional<Dictation> dictation(std::int64_t id) const;
    // Newest first; `query` filters by text (empty: all).
    // Dictations, newest first (not transcribed files).
    std::vector<Dictation> history(int limit, const std::string& query = {}) const;
    // Transcribed files (status "file", app "file:<name>"), newest first.
    std::vector<Dictation> files(int limit) const;
    void remove(std::int64_t id);
    void clearHistory();
    // Recordings older than `days` lose their audio (the text stays).
    std::vector<std::string> expireAudio(int days);

    std::vector<Word> words() const;
    std::int64_t addWord(const Word& w);
    void updateWord(const Word& w);
    void removeWord(std::int64_t id);

    std::vector<Snippet> snippets() const;
    std::int64_t addSnippet(const Snippet& s);
    void updateSnippet(const Snippet& s);
    void removeSnippet(std::int64_t id);

    // Totals for the Home page.
    struct Stats {
        std::int64_t words = 0;
        std::int64_t dictations = 0;
        double seconds = 0;
    };
    Stats stats() const;

private:
    void exec(const char* sql);
    sqlite3* db_ = nullptr;
};

} // namespace tala
