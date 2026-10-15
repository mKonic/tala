#pragma once
// From what the model heard to what gets pasted. Pure functions of the
// transcript and the user's lists, so each step can be tested on its own.
//
// Phonon-2 already punctuates, capitalises and writes numbers as digits; the
// steps here are the rest of what Wispr Flow's and Detta's formatting does
// that rules can do well: spoken commands, fillers and stutters, "scratch
// that", the dictionary's corrections, snippets and the writing style.

#include <QList>
#include <QString>

namespace tala {

enum class Style {
    Formal,      // caps and punctuation as transcribed
    Casual,      // no final period
    VeryCasual,  // no final period, lowercase sentence starts
};

// Where the text goes, as Flow sorts apps for its styles.
enum class Category { Personal, Work, Email, Other };

struct Correction {
    QString from;  // what keeps coming out wrong
    QString to;    // the spelling wanted
};

struct SnippetRule {
    QString trigger;
    QString expansion;
};

struct PipelineOptions {
    bool commands = true;  // "new line", "comma", ...
    bool fillers = true;   // um, uh, stutters, "scratch that"
    Style style = Style::Formal;
    QList<Correction> corrections;
    QList<SnippetRule> snippets;
};

namespace pipeline {

QString commands(const QString& text);
QString fillers(const QString& text);
QString backtrack(const QString& text);
QString corrections(const QString& text, const QList<Correction>& rules);
QString snippets(const QString& text, const QList<SnippetRule>& rules);
QString style(const QString& text, Style s);
// Spaces and capitals put right after the other steps cut things out.
QString tidy(const QString& text);

// All of the above, in order.
QString run(const QString& raw, const PipelineOptions& o);

// Joining onto what the same field got a moment ago: a space between
// sentences, nothing at the start of a line.
QString continuing(const QString& previous, const QString& next);

// Fitted to the text before the caret: a space after a word, none at the
// start of a line or after an opening bracket; mid-sentence, the model's
// sentence-start capital comes off (not "I", not an acronym).
QString fit(const QString& before, const QString& next);

// The app (its app id, and in a browser the window title) as Flow's
// categories: messengers, work chat, email, the rest.
Category category(const QString& appId, const QString& title);
const char* categoryKey(Category c);  // "personal", "work", "email", "other"

} // namespace pipeline

} // namespace tala
