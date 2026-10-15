#include "store.hpp"

#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <utility>

namespace tala {

namespace {

// One prepared statement, finalized on scope exit.
class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) {
        if (db && sqlite3_prepare_v2(db, sql, -1, &s_, nullptr) != SQLITE_OK) {
            std::fprintf(stderr, "tala: sql: %s: %s\n", sqlite3_errmsg(db), sql);
            s_ = nullptr;
        }
    }
    ~Stmt() { sqlite3_finalize(s_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    explicit operator bool() const { return s_; }
    Stmt& bind(int i, const std::string& v) {
        sqlite3_bind_text(s_, i, v.data(), int(v.size()), SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& bind(int i, std::int64_t v) {
        sqlite3_bind_int64(s_, i, v);
        return *this;
    }
    Stmt& bind(int i, double v) {
        sqlite3_bind_double(s_, i, v);
        return *this;
    }
    bool step() { return s_ && sqlite3_step(s_) == SQLITE_ROW; }
    void run() {
        if (s_)
            sqlite3_step(s_);
    }
    std::string text(int i) const {
        const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(s_, i));
        return p ? std::string(p, std::size_t(sqlite3_column_bytes(s_, i))) : std::string();
    }
    std::int64_t integer(int i) const { return sqlite3_column_int64(s_, i); }
    double real(int i) const { return sqlite3_column_double(s_, i); }

private:
    sqlite3_stmt* s_ = nullptr;
};

constexpr const char* kSchema = R"sql(
CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS history (
    id INTEGER PRIMARY KEY, time INTEGER NOT NULL, app TEXT NOT NULL DEFAULT '',
    raw TEXT NOT NULL DEFAULT '', text TEXT NOT NULL DEFAULT '', seconds REAL NOT NULL DEFAULT 0,
    status TEXT NOT NULL DEFAULT 'ok', audio TEXT NOT NULL DEFAULT '');
CREATE INDEX IF NOT EXISTS history_time ON history(time);
CREATE TABLE IF NOT EXISTS dictionary (
    id INTEGER PRIMARY KEY, word TEXT NOT NULL, misspelling TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS snippets (
    id INTEGER PRIMARY KEY, trigger TEXT NOT NULL, expansion TEXT NOT NULL);
)sql";

Dictation readDictation(const Stmt& s) {
    return {s.integer(0), s.integer(1), s.text(2), s.text(3), s.text(4), s.real(5), s.text(6), s.text(7)};
}

} // namespace

Store::Store(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::fprintf(stderr, "tala: can't open %s: %s\n", path.c_str(), sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
        return;
    }
    sqlite3_busy_timeout(db_, 2000);
    exec("PRAGMA journal_mode=WAL");
    exec(kSchema);
}

Store::~Store() {
    sqlite3_close(db_);
}

void Store::exec(const char* sql) {
    char* err = nullptr;
    if (db_ && sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::fprintf(stderr, "tala: sql: %s\n", err ? err : "?");
        sqlite3_free(err);
    }
}

std::optional<std::string> Store::setting(const std::string& key) const {
    Stmt s(db_, "SELECT value FROM settings WHERE key = ?");
    if (s && s.bind(1, key).step())
        return s.text(0);
    return std::nullopt;
}

std::string Store::setting(const std::string& key, const std::string& fallback) const {
    return setting(key).value_or(fallback);
}

bool Store::flag(const std::string& key, bool fallback) const {
    const auto v = setting(key);
    return v ? *v == "1" || *v == "true" : fallback;
}

void Store::set(const std::string& key, const std::string& value) {
    Stmt(db_, "INSERT INTO settings(key, value) VALUES(?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value")
        .bind(1, key)
        .bind(2, value)
        .run();
}

std::int64_t Store::add(const Dictation& d) {
    Stmt s(db_, "INSERT INTO history(time, app, raw, text, seconds, status, audio) VALUES(?, ?, ?, ?, ?, ?, ?)");
    s.bind(1, d.time).bind(2, d.app).bind(3, d.raw).bind(4, d.text).bind(5, d.seconds).bind(6, d.status).bind(7, d.audio);
    s.run();
    return db_ ? sqlite3_last_insert_rowid(db_) : 0;
}

void Store::update(const Dictation& d) {
    Stmt s(db_, "UPDATE history SET time = ?, app = ?, raw = ?, text = ?, seconds = ?, status = ?, audio = ? WHERE id = ?");
    s.bind(1, d.time).bind(2, d.app).bind(3, d.raw).bind(4, d.text).bind(5, d.seconds).bind(6, d.status).bind(7, d.audio);
    s.bind(8, d.id).run();
}

std::optional<Dictation> Store::dictation(std::int64_t id) const {
    Stmt s(db_, "SELECT id, time, app, raw, text, seconds, status, audio FROM history WHERE id = ?");
    if (s && s.bind(1, id).step())
        return readDictation(s);
    return std::nullopt;
}

std::vector<Dictation> Store::history(int limit, const std::string& query) const {
    std::vector<Dictation> out;
    Stmt s(db_, query.empty()
                    ? "SELECT id, time, app, raw, text, seconds, status, audio FROM history WHERE status != 'file' "
                      "ORDER BY time DESC LIMIT ?"
                    : "SELECT id, time, app, raw, text, seconds, status, audio FROM history "
                      "WHERE status != 'file' AND text LIKE '%' || ? || '%' ESCAPE '\\' ORDER BY time DESC LIMIT ?");
    if (!s)
        return out;
    if (query.empty()) {
        s.bind(1, std::int64_t(limit));
    } else {
        std::string escaped;
        for (char c : query) {
            if (c == '%' || c == '_' || c == '\\')
                escaped += '\\';
            escaped += c;
        }
        s.bind(1, escaped).bind(2, std::int64_t(limit));
    }
    while (s.step())
        out.push_back(readDictation(s));
    return out;
}

void Store::remove(std::int64_t id) {
    Stmt(db_, "DELETE FROM history WHERE id = ?").bind(1, id).run();
}

void Store::clearHistory() {
    exec("DELETE FROM history");
}

std::vector<std::string> Store::expireAudio(int days) {
    using namespace std::chrono;
    const std::int64_t cutoff =
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() - std::int64_t(days) * 86'400'000;
    std::vector<std::string> gone;
    {
        Stmt s(db_, "SELECT audio FROM history WHERE time < ? AND audio != ''");
        s.bind(1, cutoff);
        while (s.step())
            gone.push_back(s.text(0));
    }
    Stmt(db_, "UPDATE history SET audio = '' WHERE time < ?").bind(1, cutoff).run();
    return gone;
}

std::vector<Word> Store::words() const {
    std::vector<Word> out;
    Stmt s(db_, "SELECT id, word, misspelling FROM dictionary ORDER BY word COLLATE NOCASE");
    while (s.step())
        out.push_back({s.integer(0), s.text(1), s.text(2)});
    return out;
}

std::int64_t Store::addWord(const Word& w) {
    Stmt(db_, "INSERT INTO dictionary(word, misspelling) VALUES(?, ?)").bind(1, w.word).bind(2, w.misspelling).run();
    return db_ ? sqlite3_last_insert_rowid(db_) : 0;
}

void Store::updateWord(const Word& w) {
    Stmt(db_, "UPDATE dictionary SET word = ?, misspelling = ? WHERE id = ?")
        .bind(1, w.word)
        .bind(2, w.misspelling)
        .bind(3, w.id)
        .run();
}

void Store::removeWord(std::int64_t id) {
    Stmt(db_, "DELETE FROM dictionary WHERE id = ?").bind(1, id).run();
}

std::vector<Snippet> Store::snippets() const {
    std::vector<Snippet> out;
    Stmt s(db_, "SELECT id, trigger, expansion FROM snippets ORDER BY trigger COLLATE NOCASE");
    while (s.step())
        out.push_back({s.integer(0), s.text(1), s.text(2)});
    return out;
}

std::int64_t Store::addSnippet(const Snippet& sn) {
    Stmt(db_, "INSERT INTO snippets(trigger, expansion) VALUES(?, ?)").bind(1, sn.trigger).bind(2, sn.expansion).run();
    return db_ ? sqlite3_last_insert_rowid(db_) : 0;
}

void Store::updateSnippet(const Snippet& sn) {
    Stmt(db_, "UPDATE snippets SET trigger = ?, expansion = ? WHERE id = ?")
        .bind(1, sn.trigger)
        .bind(2, sn.expansion)
        .bind(3, sn.id)
        .run();
}

void Store::removeSnippet(std::int64_t id) {
    Stmt(db_, "DELETE FROM snippets WHERE id = ?").bind(1, id).run();
}

std::vector<Dictation> Store::files(int limit) const {
    std::vector<Dictation> out;
    Stmt s(db_, "SELECT id, time, app, raw, text, seconds, status, audio FROM history WHERE status = 'file' "
                "ORDER BY time DESC LIMIT ?");
    if (!s)
        return out;
    s.bind(1, std::int64_t(limit));
    while (s.step())
        out.push_back(readDictation(s));
    return out;
}

Store::Stats Store::stats() const {
    Stats st;
    Stmt s(db_, "SELECT text, seconds FROM history WHERE status = 'ok'");
    while (s.step()) {
        const std::string t = s.text(0);
        bool in = false;
        for (unsigned char c : t) {
            const bool space = c == ' ' || c == '\n' || c == '\t';
            if (!space && !in)
                ++st.words;
            in = !space;
        }
        ++st.dictations;
        st.seconds += s.real(1);
    }
    return st;
}

} // namespace tala
