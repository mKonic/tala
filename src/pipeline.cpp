#include "pipeline.hpp"

#include <QRegularExpression>

#include <algorithm>

namespace tala::pipeline {

namespace {

using RE = QRegularExpression;
constexpr auto CI = RE::CaseInsensitiveOption;

// Punctuation the model put around a spoken command, which the command
// replaces: a comma before it, anything after it. A sentence's own end stays.
const QString kBefore = QStringLiteral("[\\s,]*");
const QString kAfter = QStringLiteral("[\\s,.;:!?]*");

QString replaceAll(QString text, const RE& re, const QString& with) {
    return text.replace(re, with);
}

bool sentenceEnd(QChar c) {
    return c == '.' || c == '!' || c == '?' || c == '\n';
}

// Capitalises the first letter after the start and after each sentence end.
QString capitaliseSentences(QString t) {
    bool start = true;
    for (qsizetype i = 0; i < t.size(); ++i) {
        const QChar c = t[i];
        if (start && c.isLetter()) {
            t[i] = c.toUpper();
            start = false;
        } else if (sentenceEnd(c)) {
            start = true;
        } else if (!c.isSpace() && c != '"' && c != '(' && c != '\'') {
            start = false;
        }
    }
    return t;
}

} // namespace

QString commands(const QString& text) {
    QString t = text;
    static const RE paragraph(kBefore + "\\b(?:start a )?new paragraph\\b" + kAfter, CI);
    static const RE line(kBefore + "\\b(?:new line|next line|line break|skip a line)\\b" + kAfter, CI);
    t = replaceAll(t, paragraph, QStringLiteral("\n\n"));
    t = replaceAll(t, line, QStringLiteral("\n"));

    // Marks said by name. Not after "a"/"the" ("put a comma here").
    struct Mark {
        const char* words;
        const char* mark;
    };
    static const Mark marks[] = {
        {"question mark", "?"}, {"exclamation (?:point|mark)", "!"}, {"full stop", "."}, {"period", "."},
        {"comma", ","},         {"semicolon", ";"},                  {"colon", ":"},
        {"em ?-?dash", "—"},
    };
    static const RE article(QStringLiteral("\\b(?:a|an|the|this|that|one)$"), CI);
    for (const Mark& m : marks) {
        const QString mark = QString::fromUtf8(m.mark);
        const RE re(QStringLiteral("[\\s,]*\\b(?:%1)\\b[.,;:!?]*").arg(QString::fromUtf8(m.words)), CI);
        QString out;
        qsizetype from = 0;
        for (auto it = re.globalMatch(t); it.hasNext();) {
            const auto match = it.next();
            if (article.match(t.left(match.capturedStart()).trimmed()).hasMatch())
                continue;
            out += t.mid(from, match.capturedStart() - from) + mark;
            from = match.capturedEnd();
            // A mark is followed by a space unless the text ends or a line breaks.
            if (from < t.size() && t[from] != '\n' && mark != QStringLiteral("—"))
                out += ' ';
        }
        out += t.mid(from);
        t = out;
    }
    return t;
}

QString fillers(const QString& text) {
    QString t = text;
    // um, uh, erm, er, hmm, mm: with the commas the model put around them.
    static const RE filler(QStringLiteral("(?:,\\s*)?\\b(?:u+m+|u+h+|e+r+m+|e+r+|h+m+|m+h*m+)\\b[,.]?(?=\\s|$)"), CI);
    t = replaceAll(t, filler, QString());
    // "we we should" -> "we should"; "that that" and "had had" can be meant.
    static const RE stutter(QStringLiteral("\\b(\\w+)(?:[,\\s]+\\1\\b)+"), CI);
    QString out;
    qsizetype from = 0;
    for (auto it = stutter.globalMatch(t); it.hasNext();) {
        const auto m = it.next();
        const QString w = m.captured(1).toLower();
        if (w == "that" || w == "had" || w.at(0).isDigit())
            continue;
        out += t.mid(from, m.capturedStart() - from) + m.captured(1);
        from = m.capturedEnd();
    }
    out += t.mid(from);
    return backtrack(out);
}

QString backtrack(const QString& text) {
    static const RE scratch(QStringLiteral("[\\s,.;:!?]*\\bscratch that\\b[\\s,.;:!?]*"), CI);
    QString t = text;
    for (;;) {
        const auto m = scratch.match(t);
        if (!m.hasMatch())
            break;
        QString before = t.left(m.capturedStart());
        // Drop the sentence being taken back: back to the last sentence end
        // that has something before the phrase after it.
        qsizetype cut = before.size();
        while (cut > 0 && !sentenceEnd(before[cut - 1]))
            --cut;
        before = before.left(cut);
        QString after = t.mid(m.capturedEnd());
        t = before.isEmpty() || before.endsWith('\n') ? before + after : before + ' ' + after;
    }
    return t;
}

QString corrections(const QString& text, const QList<Correction>& rules) {
    QList<Correction> sorted = rules;
    // The longer phrase wins where two overlap.
    std::ranges::sort(sorted, [](const Correction& a, const Correction& b) { return a.from.size() > b.from.size(); });
    QString t = text;
    for (const Correction& c : sorted) {
        if (c.from.trimmed().isEmpty())
            continue;
        // Whole words; "n't" contractions are left alone ("Don" isn't "don't").
        const RE re(QStringLiteral("(?<![\\w'’])%1(?![\\w]|['’]t\\b)").arg(RE::escape(c.from.trimmed())), CI);
        t = replaceAll(t, re, c.to.trimmed());
    }
    return t;
}

QString snippets(const QString& text, const QList<SnippetRule>& rules) {
    QList<SnippetRule> sorted = rules;
    std::ranges::sort(sorted, [](const SnippetRule& a, const SnippetRule& b) { return a.trigger.size() > b.trigger.size(); });
    // Said on its own: exactly the expansion, whatever the punctuation.
    static const RE noise(QStringLiteral("[^\\w\\s]"));
    const QString bare = QString(text).remove(noise).simplified();
    for (const SnippetRule& s : sorted)
        if (!s.trigger.trimmed().isEmpty() && bare.compare(QString(s.trigger).remove(noise).simplified(), Qt::CaseInsensitive) == 0)
            return s.expansion;
    QString t = text;
    for (const SnippetRule& s : sorted) {
        if (s.trigger.trimmed().isEmpty())
            continue;
        const RE re(QStringLiteral("(?<![\\w])%1(?![\\w])").arg(RE::escape(s.trigger.trimmed())), CI);
        t = replaceAll(t, re, s.expansion);
    }
    return t;
}

QString style(const QString& text, Style s) {
    if (s == Style::Formal)
        return text;
    QString t = text;
    // The final period goes (an ellipsis too); ? and ! stay.
    while (t.endsWith('.'))
        t.chop(1);
    if (s == Style::VeryCasual) {
        bool start = true;
        for (qsizetype i = 0; i < t.size(); ++i) {
            const QChar c = t[i];
            if (start && c.isLetter()) {
                // "I" and words in capitals (names, acronyms) keep theirs.
                qsizetype end = i;
                while (end < t.size() && (t[end].isLetterOrNumber() || t[end] == '\''))
                    ++end;
                const QString word = t.mid(i, end - i);
                const bool keep = word == "I" || word.startsWith("I'") ||
                                  (word.size() > 1 && word.mid(1) != word.mid(1).toLower());
                if (!keep)
                    t[i] = c.toLower();
                start = false;
            } else if (sentenceEnd(c)) {
                start = true;
            } else if (!c.isSpace()) {
                start = false;
            }
        }
    }
    return t;
}

QString tidy(const QString& text) {
    QString t = text;
    static const RE spaces(QStringLiteral("[ \\t]+"));
    static const RE spaceBeforeMark(QStringLiteral("[ \\t]+([,.;:!?])"));
    static const RE doubled(QStringLiteral("([,;:])[ \\t]*[,.;:]+"));
    static const RE lineSpaces(QStringLiteral("[ \\t]*\\n[ \\t]*"));
    static const RE leadingMarks(QStringLiteral("^[\\s,.;:]+"));
    t = replaceAll(t, spaces, QStringLiteral(" "));
    t = replaceAll(t, spaceBeforeMark, QStringLiteral("\\1"));
    t = replaceAll(t, doubled, QStringLiteral("\\1"));
    t = replaceAll(t, lineSpaces, QStringLiteral("\n"));
    t = replaceAll(t, leadingMarks, QString());
    // A comma left at a sentence end: "five, ." -> "five."
    static const RE commaEnd(QStringLiteral(",([.!?])"));
    t = replaceAll(t, commaEnd, QStringLiteral("\\1"));
    while (t.endsWith(' ') || t.endsWith(','))
        t.chop(1);
    return capitaliseSentences(t);
}

QString run(const QString& raw, const PipelineOptions& o) {
    QString t = raw.trimmed();
    if (o.commands)
        t = commands(t);
    if (o.fillers)
        t = fillers(t);
    t = corrections(t, o.corrections);
    t = tidy(t);
    t = snippets(t, o.snippets);
    return style(t, o.style);
}

QString continuing(const QString& previous, const QString& next) {
    if (previous.isEmpty() || next.isEmpty() || previous.endsWith('\n') || previous.endsWith(' ') || next.startsWith('\n'))
        return next;
    return ' ' + next;
}

QString fit(const QString& before, const QString& next) {
    if (next.isEmpty())
        return next;
    QString out = next;
    const bool lineStart = before.isEmpty() || before.endsWith('\n');
    const QString trimmed = before.trimmed();
    const bool sentenceStart = lineStart || trimmed.isEmpty() || QStringLiteral(".!?…").contains(trimmed.back());
    if (!sentenceStart) {
        // The first word, up to a space or punctuation.
        qsizetype end = 0;
        while (end < out.size() && (out[end].isLetterOrNumber() || out[end] == '\'' || out[end] == u'’'))
            ++end;
        const QString word = out.left(end);
        const QString base = word.section(QRegularExpression("['’]"), 0, 0);
        const bool acronym = word.size() > 1 && word == word.toUpper() && word != word.toLower();
        const bool innerCaps = std::any_of(word.begin() + std::min<qsizetype>(1, word.size()), word.end(),
                                           [](QChar c) { return c.isUpper(); });
        if (!word.isEmpty() && word[0].isUpper() && base != "I" && !acronym && !innerCaps)
            out[0] = out[0].toLower();
    }
    const QChar last = before.isEmpty() ? QChar() : before.back();
    const bool joinsAfter = !lineStart && !last.isSpace() && !QStringLiteral("([{\"'“‘/-").contains(last);
    if (joinsAfter && !QStringLiteral(",.;:!?)]}…").contains(out[0]))
        out.prepend(' ');
    return out;
}

Category category(const QString& appId, const QString& title) {
    const QString a = appId.toLower(), t = title.toLower();
    const auto any = [](const QString& s, std::initializer_list<const char*> words) {
        return std::any_of(words.begin(), words.end(), [&](const char* w) { return s.contains(QLatin1String(w)); });
    };
    if (any(a, {"thunderbird", "betterbird", "evolution", "geary", "mailspring", "kmail", "claws-mail", "mailnag"}))
        return Category::Email;
    if (any(a, {"slack", "teams", "mattermost", "zulip", "rocket.chat", "zoom", "lark", "webex"}))
        return Category::Work;
    if (any(a, {"discord", "vesktop", "telegram", "signal", "whatsapp", "element", "fractal", "beeper", "messenger",
                "dino", "gajim", "nheko", "cinny"}))
        return Category::Personal;
    // A web app: the browser's window title names the site.
    if (any(a, {"chrom", "firefox", "brave", "vivaldi", "zen", "librewolf", "edge", "epiphany", "falkon",
                "qutebrowser", "floorp", "opera", "waterfox"})) {
        if (any(t, {"gmail", "outlook", "proton mail", "fastmail", "yahoo mail", "zoho mail", "tuta"}))
            return Category::Email;
        if (any(t, {"slack", "microsoft teams", "google chat", "mattermost", "zoom"}))
            return Category::Work;
        if (any(t, {"whatsapp", "messenger", "discord", "telegram", "instagram", "signal"}))
            return Category::Personal;
    }
    return Category::Other;
}

const char* categoryKey(Category c) {
    switch (c) {
    case Category::Personal: return "personal";
    case Category::Work: return "work";
    case Category::Email: return "email";
    case Category::Other: break;
    }
    return "other";
}

} // namespace tala::pipeline
