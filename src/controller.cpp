#include "controller.hpp"

#include "inserter.hpp"
#include "recorder.hpp"
#include "shortcuts.hpp"
#include "version.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace tala {

namespace {

// A tap shorter than this is not a dictation (Flow: half a second).
constexpr int kTapMs = 500;
constexpr int kLimitMs = 20 * 60 * 1000;
// Under this nothing was said, or the mic is muted.
constexpr double kSilentPeak = 0.12;
constexpr double kMinSeconds = 0.25;
// Dictations this close in the same app join up as one text.
constexpr qint64 kJoinMs = 90 * 1000;

std::int64_t nowMs() {
    return QDateTime::currentMSecsSinceEpoch();
}

QVariantMap toMap(const Dictation& d) {
    return {{"id", qint64(d.id)},          {"time", qint64(d.time)},
            {"app", QString::fromStdString(d.app)},   {"raw", QString::fromStdString(d.raw)},
            {"text", QString::fromStdString(d.text)}, {"seconds", d.seconds},
            {"status", QString::fromStdString(d.status)}, {"hasAudio", !d.audio.empty()}};
}

} // namespace

Controller::Controller(Store& store, QObject* parent)
    : QObject(parent),
      store_(store),
      recorder_(std::make_unique<Recorder>()),
      inserter_(std::make_unique<Inserter>()),
      engine_(std::make_unique<ServerEngine>()),
      shortcuts_(std::make_unique<Shortcuts>()) {
    QDir().mkpath(audioDir());
    for (const std::string& path : store_.expireAudio(store_.setting("keep_audio_days", "14").empty()
                                                          ? 14
                                                          : std::stoi(store_.setting("keep_audio_days", "14"))))
        QFile::remove(QString::fromStdString(path));

    connect(recorder_.get(), &Recorder::level, this, [this](double v) {
        level_ = v;
        peak_ = std::max(peak_, v);
        emit levelChanged();
    });
    connect(recorder_.get(), &Recorder::devicesChanged, this, &Controller::devicesChanged);
    connect(recorder_.get(), &Recorder::lost, this, [this] {
        if (state_ == State::Recording) {
            emit notice(tr("The microphone went away: here's what it heard."));
            finish();
        }
    });
    connect(engine_.get(), &Engine::stateChanged, this, &Controller::engineChanged);

    tapWindow_.setSingleShot(true);
    tapWindow_.setInterval(kTapMs);
    connect(&tapWindow_, &QTimer::timeout, this, [this] {
        // A lone quick tap: nothing meant.
        if (state_ == State::Recording && !locked_)
            discard(false);
    });
    limit_.setSingleShot(true);
    limit_.setInterval(kLimitMs);
    connect(&limit_, &QTimer::timeout, this, [this] {
        emit notice(tr("Dictations stop at 20 minutes."));
        finish();
    });

    connect(shortcuts_.get(), &Shortcuts::pressed, this, &Controller::onPressed);
    connect(shortcuts_.get(), &Shortcuts::released, this, &Controller::onReleased);
    connect(shortcuts_.get(), &Shortcuts::triggersChanged, this, &Controller::triggersChanged);
    connect(shortcuts_.get(), &Shortcuts::failed, this, [this](const QString& why) {
        setProblem(tr("No global shortcuts: %1").arg(why));
    });
    // Flow's Windows keys, with Super for Win. Paste/copy last are Flow's
    // Windows ones too: they don't collide with the dictation chords.
    shortcuts_->bind({
        {"dictate", tr("Dictate (hold; double-tap for hands-free)"), "CTRL+LOGO+d"},
        {"hands-free", tr("Hands-free dictation (press to start and stop)"), "CTRL+LOGO+space"},
        {"cancel", tr("Cancel dictation"), "CTRL+LOGO+BackSpace"},
        {"paste-last", tr("Paste last transcript"), "SHIFT+ALT+z"},
        {"copy-last", tr("Copy last transcript"), "SHIFT+ALT+x"},
    });

    if (const QString m = inserter_->missing(); !m.isEmpty())
        setProblem(tr("Can't paste: %1").arg(m));
    else if (!recorder_->ok())
        setProblem(tr("Can't reach PipeWire for the microphone"));
    // The model takes a moment to load: start now, not at the first dictation.
    engine_->start();
}

Controller::~Controller() {
    if (state_ == State::Recording)
        recorder_->stop();
}

QString Controller::version() const {
    return QStringLiteral(TALA_VERSION);
}

QString Controller::state() const {
    switch (state_) {
    case State::Idle: return QStringLiteral("idle");
    case State::Recording: return QStringLiteral("recording");
    case State::Processing: return QStringLiteral("processing");
    }
    return {};
}

QString Controller::engineStatus() const {
    return engine_->status();
}

bool Controller::engineReady() const {
    return engine_->state() == Engine::State::Ready;
}

QVariantMap Controller::triggers() const {
    return shortcuts_->triggers();
}

QVariantList Controller::devices() const {
    return recorder_->devices();
}

QString Controller::audioDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/tala/audio";
}

void Controller::setState(State s) {
    if (state_ == s)
        return;
    state_ = s;
    if (s != State::Recording) {
        level_ = 0;
        emit levelChanged();
    }
    emit stateChanged();
}

void Controller::setProblem(const QString& p) {
    if (problem_ == p)
        return;
    problem_ = p;
    emit problemChanged();
}

void Controller::bump() {
    ++revision_;
    emit revisionChanged();
}

void Controller::onPressed(const QString& id) {
    if (id == "dictate") {
        pttDown_ = true;
        if (state_ == State::Recording && tapWindow_.isActive()) {
            // The second tap of a double tap: hands-free from here.
            tapWindow_.stop();
            locked_ = true;
            emit stateChanged();
        } else if (state_ == State::Recording && locked_) {
            finish();
        } else if (state_ == State::Idle) {
            begin(false);
        }
    } else if (id == "hands-free") {
        if (state_ == State::Idle) {
            begin(true);
        } else if (state_ == State::Recording && !locked_) {
            // Held push-to-talk, then this: keep going without the keys.
            locked_ = true;
            tapWindow_.stop();
            emit stateChanged();
        } else if (state_ == State::Recording) {
            // Pressed again right away: Flow takes that as a cancel.
            if (held_.elapsed() < kTapMs)
                discard();
            else
                finish();
        }
    } else if (id == "cancel") {
        cancel();
    } else if (id == "paste-last") {
        pasteLast();
    } else if (id == "copy-last") {
        copyLast();
    }
}

void Controller::onReleased(const QString& id) {
    if (id != "dictate")
        return;
    pttDown_ = false;
    if (state_ != State::Recording || locked_)
        return;
    if (held_.elapsed() < kTapMs) {
        // Maybe the first of a double tap: keep listening a moment.
        if (!tapWindow_.isActive())
            tapWindow_.start();
        return;
    }
    finish();
}

void Controller::begin(bool locked) {
    if (!recorder_->start(QString::fromStdString(store_.setting("mic", "")))) {
        recorder_->chime(Recorder::Chime::Error);
        emit notice(tr("Couldn't open the microphone"));
        return;
    }
    app_ = inserter_->focusedApp();
    locked_ = locked;
    peak_ = 0;
    held_.start();
    limit_.start();
    if (store_.flag("sounds", true))
        recorder_->chime(Recorder::Chime::Start);
    setState(State::Recording);
    engine_->start();
}

void Controller::toggle() {
    if (state_ == State::Idle)
        begin(true);
    else if (state_ == State::Recording)
        finish();
}

void Controller::stop() {
    if (state_ == State::Recording)
        finish();
}

void Controller::cancel() {
    if (state_ == State::Recording)
        discard();
}

void Controller::discard(bool chime) {
    tapWindow_.stop();
    limit_.stop();
    recorder_->stop();
    locked_ = false;
    if (chime && store_.flag("sounds", true))
        recorder_->chime(Recorder::Chime::Cancel);
    setState(State::Idle);
}

void Controller::finish() {
    tapWindow_.stop();
    limit_.stop();
    std::vector<float> pcm = recorder_->stop();
    locked_ = false;
    if (store_.flag("sounds", true))
        recorder_->chime(Recorder::Chime::Stop);
    const double seconds = double(pcm.size()) / Recorder::kRate;
    if (seconds < kMinSeconds || peak_ < kSilentPeak) {
        setState(State::Idle);
        emit notice(tr("Didn't hear anything. Is your microphone muted?"));
        return;
    }
    process(std::move(pcm));
}

void Controller::process(std::vector<float> pcm, std::int64_t retryOf) {
    setState(State::Processing);
    Dictation d;
    if (retryOf) {
        if (auto old = store_.dictation(retryOf))
            d = *old;
    } else {
        d.time = nowMs();
        d.app = app_.toStdString();
        d.seconds = double(pcm.size()) / Recorder::kRate;
        // Kept for a retry (and Flow's 14 days).
        const QString path = audioDir() + QStringLiteral("/%1.wav").arg(d.time);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly) && f.write(wavBytes(pcm)) > 0)
            d.audio = path.toStdString();
    }
    engine_->transcribe(pcm, hotwords(), [this, d, retryOf](const Transcript& t) mutable {
        if (!t.ok) {
            d.status = "failed";
            if (retryOf)
                store_.update(d);
            else
                d.id = store_.add(d);
            bump();
            setState(State::Idle);
            if (store_.flag("sounds", true))
                recorder_->chime(Recorder::Chime::Error);
            emit notice(tr("Transcription failed: %1. It's saved in History.").arg(t.error));
            return;
        }
        d.raw = t.text.toStdString();
        d.text = pipeline::run(t.text, options()).toStdString();
        d.status = d.text.empty() ? "empty" : "ok";
        deliver(std::move(d), true);
    });
}

void Controller::deliver(Dictation d, bool paste) {
    if (d.id)
        store_.update(d);
    else
        d.id = store_.add(d);
    bump();
    if (d.text.empty() || !paste) {
        setState(State::Idle);
        return;
    }
    QString text = QString::fromStdString(d.text);
    const QString app = QString::fromStdString(d.app);
    if (app == lastApp_ && lastTime_.isValid() && lastTime_.elapsed() < kJoinMs)
        text = pipeline::continuing(lastText_, text);
    lastApp_ = app;
    lastText_ = text;
    lastTime_.start();
    inserter_->insert(text, [this](bool read) {
        setState(State::Idle);
        if (!read)
            emit notice(tr("Couldn't paste here. The text is on the clipboard."));
    });
}

void Controller::retry(qint64 id) {
    auto d = store_.dictation(id);
    if (!d || d->audio.empty() || state_ != State::Idle)
        return;
    std::vector<float> pcm;
    if (!readWav(QString::fromStdString(d->audio), pcm)) {
        emit notice(tr("That recording is gone."));
        return;
    }
    process(std::move(pcm), id);
}

void Controller::pasteLast() {
    const auto h = store_.history(1);
    if (h.empty() || h.front().text.empty() || state_ != State::Idle)
        return;
    inserter_->insert(QString::fromStdString(h.front().text), [](bool) {});
}

void Controller::copyLast() {
    const auto h = store_.history(1);
    if (!h.empty() && !h.front().text.empty())
        inserter_->copy(QString::fromStdString(h.front().text));
}

void Controller::copy(const QString& text) {
    inserter_->copy(text);
}

void Controller::removeDictation(qint64 id) {
    if (auto d = store_.dictation(id); d && !d->audio.empty())
        QFile::remove(QString::fromStdString(d->audio));
    store_.remove(id);
    bump();
}

void Controller::clearHistory() {
    for (const Dictation& d : store_.history(1 << 30))
        if (!d.audio.empty())
            QFile::remove(QString::fromStdString(d.audio));
    store_.clearHistory();
    bump();
}

QVariantList Controller::history(const QString& query) const {
    QVariantList out;
    for (const Dictation& d : store_.history(500, query.toStdString()))
        out.append(toMap(d));
    return out;
}

QVariantMap Controller::stats() const {
    const Store::Stats s = store_.stats();
    return {{"words", qint64(s.words)},
            {"dictations", qint64(s.dictations)},
            {"minutes", s.seconds / 60},
            {"wpm", s.seconds > 0 ? double(s.words) / (s.seconds / 60) : 0.0}};
}

QVariantList Controller::words() const {
    QVariantList out;
    for (const Word& w : store_.words())
        out.append(QVariantMap{{"id", qint64(w.id)},
                               {"word", QString::fromStdString(w.word)},
                               {"misspelling", QString::fromStdString(w.misspelling)}});
    return out;
}

QString Controller::saveWord(qint64 id, const QString& word, const QString& misspelling) {
    const QString w = word.trimmed(), m = misspelling.trimmed();
    if (w.isEmpty())
        return tr("Enter a word");
    if (w.size() > 60 || m.size() > 60)
        return tr("Keep it under 60 characters");
    for (const Word& other : store_.words())
        if (other.id != id && ((m.isEmpty() && QString::fromStdString(other.word).compare(w, Qt::CaseInsensitive) == 0 &&
                                other.misspelling.empty()) ||
                               (!m.isEmpty() && QString::fromStdString(other.misspelling).compare(m, Qt::CaseInsensitive) == 0)))
            return tr("Already in your dictionary");
    Word entry{id, w.toStdString(), m.toStdString()};
    if (id)
        store_.updateWord(entry);
    else
        store_.addWord(entry);
    bump();
    return {};
}

void Controller::removeWord(qint64 id) {
    store_.removeWord(id);
    bump();
}

QVariantList Controller::snippetList() const {
    QVariantList out;
    for (const Snippet& s : store_.snippets())
        out.append(QVariantMap{{"id", qint64(s.id)},
                               {"trigger", QString::fromStdString(s.trigger)},
                               {"expansion", QString::fromStdString(s.expansion)}});
    return out;
}

QString Controller::saveSnippet(qint64 id, const QString& trigger, const QString& expansion) {
    const QString t = trigger.trimmed();
    if (t.isEmpty() || expansion.trimmed().isEmpty())
        return tr("Enter a snippet and its expansion");
    if (t.size() > 60)
        return tr("Keep the trigger under 60 characters");
    if (expansion.size() > 4000)
        return tr("This expansion is too long");
    for (const Snippet& other : store_.snippets())
        if (other.id != id && QString::fromStdString(other.trigger).compare(t, Qt::CaseInsensitive) == 0)
            return tr("\"%1\" already exists").arg(t);
    Snippet s{id, t.toStdString(), expansion.toStdString()};
    if (id)
        store_.updateSnippet(s);
    else
        store_.addSnippet(s);
    bump();
    return {};
}

void Controller::removeSnippet(qint64 id) {
    store_.removeSnippet(id);
    bump();
}

QString Controller::setting(const QString& key, const QString& fallback) const {
    return QString::fromStdString(store_.setting(key.toStdString(), fallback.toStdString()));
}

void Controller::setSetting(const QString& key, const QString& value) {
    store_.set(key.toStdString(), value.toStdString());
    bump();
}

void Controller::configureShortcuts() {
    shortcuts_->configure();
}

PipelineOptions Controller::options() const {
    PipelineOptions o;
    o.commands = store_.flag("commands", true);
    o.fillers = store_.flag("fillers", true);
    const std::string style = store_.setting("style", "formal");
    o.style = style == "casual" ? Style::Casual : style == "very_casual" ? Style::VeryCasual : Style::Formal;
    for (const Word& w : store_.words())
        if (!w.misspelling.empty())
            o.corrections.append({QString::fromStdString(w.misspelling), QString::fromStdString(w.word)});
    for (const Snippet& s : store_.snippets())
        o.snippets.append({QString::fromStdString(s.trigger), QString::fromStdString(s.expansion)});
    return o;
}

QStringList Controller::hotwords() const {
    QStringList out;
    for (const Word& w : store_.words())
        out.append(QString::fromStdString(w.word));
    return out;
}

QString Controller::preview(const QString& text) const {
    return pipeline::run(text, options());
}

} // namespace tala
