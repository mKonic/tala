#include "controller.hpp"

#include "inserter.hpp"
#include "recorder.hpp"
#include "shortcuts.hpp"
#include "version.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
      engine_(std::make_unique<Engine>()),
      shortcuts_(std::make_unique<Shortcuts>()),
      context_(std::make_unique<Context>()),
      decoder_(std::make_unique<FileDecoder>()),
      screenshot_(std::make_unique<Screenshot>()),
      cleanup_(std::make_unique<Cleanup>()) {
    connect(cleanup_.get(), &Cleanup::stateChanged, this, &Controller::cleanupChanged);
    cleanup_->setIdleMinutes(QString::fromStdString(store_.setting("engine_idle_minutes", "5")).toInt());
    applyCleanupLevel();
    cleanup_->setCommands(store_.flag("command_mode", false));
    noteWindow_.setSingleShot(true);
    noteWindow_.setInterval(kTapMs);
    context_->setEnabled(store_.flag("context", false));
    QDir().mkpath(audioDir());
    expireAudio();

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
        // Flow's own: Ctrl+Win held alone. Only atrium binds modifiers alone.
        {"dictate", tr("Dictate (hold; double-tap for hands-free)"),
         Shortcuts::escapeWorks() ? "CTRL+LOGO" : "CTRL+LOGO+d"},
        {"hands-free", tr("Hands-free dictation (press to start and stop)"), "CTRL+LOGO+space"},
        {"cancel", tr("Cancel dictation"), "CTRL+LOGO+BackSpace"},
        // Flow's Ctrl+Win+Alt, held.
        {"command", tr("Command Mode (hold, say what to do with the selection)"),
         Shortcuts::escapeWorks() ? "CTRL+LOGO+ALT" : "CTRL+ALT+LOGO+d"},
        {"paste-last", tr("Paste last transcript"), "SHIFT+ALT+z"},
        {"copy-last", tr("Copy last transcript"), "SHIFT+ALT+x"},
    });

    if (const QString m = inserter_->missing(); !m.isEmpty())
        setProblem(tr("Can't paste: %1").arg(m));
    else if (!recorder_->ok() && qEnvironmentVariableIsEmpty("TALA_FAKE_MIC"))
        setProblem(tr("Can't reach PipeWire for the microphone"));
    engine_->setIdleMinutes(QString::fromStdString(store_.setting("engine_idle_minutes", "5")).toInt());
    // Nothing to download: the engine starts with the first dictation.
    // Otherwise the download starts now, not when the user first speaks.
    if (engine_->state() == Engine::State::Missing)
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
    // Stopped is ready too: it loads in a fraction of a second.
    return engine_->state() == Engine::State::Ready || engine_->state() == Engine::State::Stopped;
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
    shortcuts_->setEscape(s == State::Recording);
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
            noteWindow_.start();
            emit stateChanged();
        } else if (state_ == State::Recording && locked_ && noteWindow_.isActive() && !note_) {
            // A third: a quick note (Detta's), kept, not pasted, with what's
            // on screen now.
            noteWindow_.stop();
            note_ = true;
            // The bar hides for the picture: a frame to clear it, then the shot.
            capturing_ = true;
            emit capturingChanged();
            QTimer::singleShot(80, this, [this] {
                screenshot_->take([this](const QString& path) {
                    shot_ = path;
                    capturing_ = false;
                    emit capturingChanged();
                    emit notice(tr("Quick note: speak, then tap again to save it."));
                });
            });
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
    } else if (id == "command") {
        if (state_ != State::Idle)
            return;
        if (!store_.flag("command_mode", false)) {
            emit notice(tr("Command Mode is off: turn it on in Tala's Settings."));
            return;
        }
        // The selection as it is now, before anything moves it.
        selection_.clear();
        inserter_->selectedText([this](const QString& s) { selection_ = s; });
        begin(false);
        command_ = state_ == State::Recording;
        if (command_)
            emit notice(selection_.isEmpty() ? tr("Say what to write") : tr("Say what to do with the selection"));
    } else if (id == "cancel" || id == "escape") {
        cancel();
    } else if (id == "paste-last") {
        pasteLast();
    } else if (id == "copy-last") {
        copyLast();
    }
}

void Controller::onReleased(const QString& id, bool cancelled) {
    if (id == "command") {
        if (state_ == State::Recording && command_)
            cancelled ? discard(false) : finish();
        return;
    }
    if (id != "dictate")
        return;
    pttDown_ = false;
    // Ctrl+Super was the start of another shortcut (Ctrl+Super+L): not a
    // dictation after all.
    if (cancelled && state_ == State::Recording && !locked_) {
        discard(false);
        return;
    }
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
    QString mic = QString::fromStdString(store_.setting("mic", ""));
    if (!mic.isEmpty() && recorder_->ok() && !recorder_->has(mic)) {
        // The chosen one isn't here (unplugged, the phone away): the default.
        emit notice(tr("Your microphone isn't connected: using the default one."));
        mic.clear();
    }
    if (!recorder_->start(mic)) {
        recorder_->chime(Recorder::Chime::Error);
        emit notice(tr("Couldn't open the microphone"));
        return;
    }
    app_ = inserter_->focusedApp();
    title_ = inserter_->focusedTitle();
    note_ = false;
    command_ = false;
    shot_.clear();
    category_ = pipeline::category(app_, inserter_->focusedTitle());
    if (const QString screen = inserter_->focusedScreen(); !screen.isEmpty())
        emit screenWanted(screen);
    locked_ = locked;
    peak_ = 0;
    held_.start();
    limit_.start();
    // A moment late: a modifier-only chord that turns out to be the start of
    // another shortcut (Ctrl+Super+L) is dropped before it chimes.
    if (store_.flag("sounds", true))
        QTimer::singleShot(150, this, [this, run = ++run_] {
            if (state_ == State::Recording && run == run_)
                recorder_->chime(Recorder::Chime::Start);
        });
    setState(State::Recording);
    engine_->start();
    cleanup_->prepare();
    if (store_.flag("command_mode", false))
        cleanup_->setCommands(true);
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
        // Kept for a retry, as long as the user lets it (Flow: 14 days).
        if (keepDays() > 0) {
            const QString path = audioDir() + QStringLiteral("/%1.wav").arg(d.time);
            QFile f(path);
            if (f.open(QIODevice::WriteOnly) && f.write(wavBytes(pcm)) > 0)
                d.audio = path.toStdString();
        }
    }
    const bool note = note_;
    note_ = false;
    const bool command = command_;
    command_ = false;
    const QString selection = selection_;
    engine_->transcribe(pcm, hotwords(), [this, d, retryOf, note, pcm, command, selection](const Transcript& t) mutable {
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
        if (command && !retryOf) {
            // Command Mode: the instruction (fillers out), then the model's
            // text in place of the selection, as it returns it.
            PipelineOptions o;
            o.commands = false;
            const QString instruction = pipeline::run(t.text, o);
            cleanup_->command(instruction, selection, hotwords(), [this, d](const QString& out, bool) mutable {
                if (out.isEmpty()) {
                    d.status = "failed";
                    d.id = store_.add(d);
                    bump();
                    setState(State::Idle);
                    emit notice(tr("Command Mode couldn't do that. Is llama-server there?"));
                    return;
                }
                d.text = out.toStdString();
                d.status = "command";
                deliver(std::move(d), true, false);
            });
            return;
        }
        if (note && !retryOf) {
            // Notes are for reading back: no spoken commands, the Other style.
            PipelineOptions o = options(Category::Other);
            o.commands = false;
            d.text = pipeline::run(t.text, o).toStdString();
            d.status = "note";
            d.id = store_.add(d);
            bump();
            setState(State::Idle);
            const QString where = saveNote(d, pcm);
            emit notice(where.isEmpty() ? tr("Couldn't save the note; it's in History.") : tr("Note saved"));
            return;
        }
        // The rules, then the model (Auto Cleanup), then what the model
        // mustn't undo: the dictionary's spellings, snippets, the style.
        const PipelineOptions o = options(category_);
        PipelineOptions rules = o;
        rules.style = Style::Formal;
        rules.snippets.clear();
        const QString base = pipeline::run(t.text, rules);
        cleanup_->clean(base, hotwords(), [this, d, o](const QString& cleaned, bool) mutable {
            QString text = pipeline::corrections(cleaned, o.corrections);
            text = pipeline::style(pipeline::snippets(text, o.snippets), o.style);
            d.text = text.toStdString();
            d.status = d.text.empty() ? "empty" : "ok";
            deliver(std::move(d), true);
        });
    });
}

void Controller::deliver(Dictation d, bool paste, bool fit) {
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
    // The field says what's before the caret; else a guess from the last
    // dictation into the same app a moment ago.
    // (Command Mode's text goes in place of a selection, as it is.)
    if (!fit) {
    } else if (const std::optional<QString> before = context_->beforeCaret()) {
        text = pipeline::fit(*before, text);
    } else if (app == lastApp_ && lastTime_.isValid() && lastTime_.elapsed() < kJoinMs) {
        text = pipeline::continuing(lastText_, text);
    }
    lastApp_ = app;
    lastText_ = text;
    lastTime_.start();
    inserter_->insert(text, [this](bool read) {
        setState(State::Idle);
        if (!read)
            emit notice(tr("Couldn't paste here. The text is on the clipboard."));
    }, !fit);
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

void Controller::transcribeFile(const QUrl& file) {
    const QString path = file.toLocalFile();
    if (path.isEmpty() || !fileStatus_.isEmpty())
        return;
    const QString name = QFileInfo(path).fileName();
    const int run = ++fileRun_;
    const auto status = [this](const QString& s, double p) {
        fileStatus_ = s;
        fileProgress_ = p;
        emit fileChanged();
    };
    status(tr("Reading %1").arg(name), 0);
    decoder_->disconnect(this);
    connect(decoder_.get(), &FileDecoder::progress, this, [status, name](double p) {
        status(tr("Reading %1").arg(name), p * 0.5);
    });
    connect(decoder_.get(), &FileDecoder::failed, this, [this, status, name](const QString& why) {
        status({}, 0);
        emit notice(tr("Couldn't read %1: %2").arg(name, why));
    });
    connect(decoder_.get(), &FileDecoder::finished, this, [this, status, name, run](const std::vector<float>& pcm) {
        if (pcm.size() < std::size_t(Recorder::kRate) / 4) {
            status({}, 0);
            emit notice(tr("%1 has no sound to transcribe.").arg(name));
            return;
        }
        const double seconds = double(pcm.size()) / Recorder::kRate;
        status(tr("Transcribing %1 (%2 min)").arg(name).arg(seconds / 60, 0, 'f', 1), 0.5);
        engine_->transcribe(pcm, hotwords(), [this, status, name, run, seconds](const Transcript& t) {
            if (run != fileRun_)
                return;  // cancelled meanwhile
            status({}, 0);
            if (!t.ok) {
                emit notice(tr("Couldn't transcribe %1: %2").arg(name, t.error));
                return;
            }
            // Words said in a recording aren't commands to tala.
            PipelineOptions o = options(Category::Other);
            o.commands = false;
            Dictation d;
            d.time = QDateTime::currentMSecsSinceEpoch();
            d.app = ("file:" + name).toStdString();
            d.raw = t.text.toStdString();
            d.text = pipeline::run(t.text, o).toStdString();
            d.seconds = seconds;
            d.status = "file";
            store_.add(d);
            bump();
        });
    });
    decoder_->decode(path);
}

void Controller::applyCleanupLevel() {
    const std::string l = store_.setting("cleanup", "none");
    cleanup_->setLevel(l == "light" ? Cleanup::Level::Light : l == "medium" ? Cleanup::Level::Medium : Cleanup::Level::None);
}

QString Controller::notesDir() const {
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return (docs.isEmpty() ? QDir::homePath() : docs) + QStringLiteral("/Tala notes");
}

void Controller::openNotes() const {
    QDir().mkpath(notesDir());
    QDesktopServices::openUrl(QUrl::fromLocalFile(notesDir()));
}

// <notes>/<date time>/note.md, recording.wav and screen.png; the folder, or
// "" when it couldn't be written.
QString Controller::saveNote(const Dictation& d, const std::vector<float>& pcm) const {
    const QDateTime when = QDateTime::fromMSecsSinceEpoch(d.time);
    const QString dir = notesDir() + '/' + when.toString("yyyy-MM-dd HH.mm.ss");
    if (!QDir().mkpath(dir))
        return {};
    QFile wav(dir + "/recording.wav");
    const bool haveAudio = wav.open(QIODevice::WriteOnly) && wav.write(wavBytes(pcm)) > 0;
    wav.close();
    bool haveShot = false;
    if (!shot_.isEmpty() && QFileInfo::exists(shot_)) {
        haveShot = QFile::copy(shot_, dir + "/screen.png");
        QFile::remove(shot_);  // the portal's copy, made for this
    }
    QString md = "---\n";
    md += "time: " + when.toString(Qt::ISODate) + "\n";
    if (!d.app.empty())
        md += "app: " + QString::fromStdString(d.app) + "\n";
    if (!title_.isEmpty())
        md += "window: \"" + QString(title_).replace('"', "\\\"") + "\"\n";
    md += QStringLiteral("seconds: %1\n").arg(d.seconds, 0, 'f', 1);
    if (haveAudio)
        md += "recording: recording.wav\n";
    if (haveShot)
        md += "screenshot: screen.png\n";
    md += "---\n\n" + QString::fromStdString(d.text) + "\n";
    if (haveShot)
        md += "\n![What was on screen](screen.png)\n";
    QFile f(dir + "/note.md");
    if (!f.open(QIODevice::WriteOnly) || f.write(md.toUtf8()) < 0)
        return {};
    return dir;
}

void Controller::cancelFile() {
    ++fileRun_;
    decoder_->cancel();
    fileStatus_.clear();
    fileProgress_ = 0;
    emit fileChanged();
}

QVariantList Controller::files() const {
    QVariantList out;
    for (const Dictation& d : store_.files(200))
        out.append(toMap(d));
    return out;
}

QString Controller::saveText(const QUrl& file, const QString& text) const {
    QFile f(file.toLocalFile());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return f.errorString();
    return f.write(text.toUtf8()) < 0 ? f.errorString() : QString();
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
    if (key == "keep_audio_days")
        expireAudio();
    if (key == "engine_idle_minutes")
        engine_->setIdleMinutes(value.toInt());
    if (key == "context")
        context_->setEnabled(value == "1");
    if (key == "cleanup")
        applyCleanupLevel();
    if (key == "command_mode")
        cleanup_->setCommands(value == "1");
    if (key == "engine_idle_minutes")
        cleanup_->setIdleMinutes(value.toInt());
    bump();
}

int Controller::keepDays() const {
    bool ok = false;
    const int days = QString::fromStdString(store_.setting("keep_audio_days", "14")).toInt(&ok);
    return ok ? std::max(days, 0) : 14;
}

void Controller::expireAudio() {
    for (const std::string& path : store_.expireAudio(keepDays()))
        QFile::remove(QString::fromStdString(path));
}

QString Controller::autostartPath() const {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           "/autostart/io.github.mkonic.Tala.desktop";
}

bool Controller::autostart() const {
    return QFile::exists(autostartPath());
}

void Controller::setAutostart(bool on) {
    if (on) {
        QDir().mkpath(QFileInfo(autostartPath()).path());
        QFile f(autostartPath());
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write("[Desktop Entry]\nType=Application\nName=Tala\nComment=Dictation, waiting for its key\n"
                    "Exec=tala --background\nIcon=io.github.mkonic.Tala\nNoDisplay=true\n");
    } else {
        QFile::remove(autostartPath());
    }
    bump();
}

void Controller::configureShortcuts() {
    shortcuts_->configure();
}

PipelineOptions Controller::options(Category c) const {
    PipelineOptions o;
    o.commands = store_.flag("commands", true);
    o.fillers = store_.flag("fillers", true);
    // Each category's own (Flow's Style tabs), else the one style set before.
    const std::string style =
        store_.setting(std::string("style.") + pipeline::categoryKey(c), store_.setting("style", "formal"));
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

QString Controller::preview(const QString& text, const QString& category) const {
    const Category c = category == "personal" ? Category::Personal
                       : category == "work"   ? Category::Work
                       : category == "email"  ? Category::Email
                                              : Category::Other;
    return pipeline::run(text, options(c));
}

} // namespace tala
