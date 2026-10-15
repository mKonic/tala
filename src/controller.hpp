#pragma once
// The dictation itself: keys in, recording, the engine, the pipeline, the
// paste, the history entry. Also everything the QML (the bar and the Hub)
// reads and calls.
//
// Keys, as in Wispr Flow:
//   push-to-talk   hold to dictate, release to paste. A quick tap waits half a
//                  second for a second one: a double tap locks it hands-free.
//   hands-free     press to start, press again to stop and paste.
//   paste last / copy last, and cancel.

#include "context.hpp"
#include "engine.hpp"
#include "filedecoder.hpp"
#include "screenshot.hpp"
#include "pipeline.hpp"
#include "store.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include <memory>

namespace tala {

class Recorder;
class Inserter;
class Shortcuts;

class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY stateChanged)
    Q_PROPERTY(double level READ level NOTIFY levelChanged)
    Q_PROPERTY(QString engineStatus READ engineStatus NOTIFY engineChanged)
    Q_PROPERTY(bool engineReady READ engineReady NOTIFY engineChanged)
    Q_PROPERTY(QVariantMap triggers READ triggers NOTIFY triggersChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    // A file being transcribed: what's happening ("" when nothing), 0..1.
    Q_PROPERTY(QString fileStatus READ fileStatus NOTIFY fileChanged)
    // A quick note's screenshot is being taken: the bar stays out of it.
    Q_PROPERTY(bool capturing READ capturing NOTIFY capturingChanged)
    Q_PROPERTY(double fileProgress READ fileProgress NOTIFY fileChanged)

public:
    Controller(Store& store, QObject* parent = nullptr);
    ~Controller() override;

    QString state() const;
    bool locked() const { return locked_; }
    double level() const { return level_; }
    QString engineStatus() const;
    bool engineReady() const;
    QVariantMap triggers() const;
    QString problem() const { return problem_; }
    int revision() const { return revision_; }
    QVariantList devices() const;
    QString version() const;

    // The bar and the Hub.
    Q_INVOKABLE void toggle();   // the bar's button: hands-free start/stop
    Q_INVOKABLE void stop();     // stop and paste
    Q_INVOKABLE void cancel();   // stop and throw away
    Q_INVOKABLE void pasteLast();
    Q_INVOKABLE void copyLast();
    Q_INVOKABLE void copy(const QString& text);
    Q_INVOKABLE void retry(qint64 id);
    Q_INVOKABLE void removeDictation(qint64 id);
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE QVariantList history(const QString& query = {}) const;
    Q_INVOKABLE QVariantMap stats() const;
    // Detta's file transcription: an audio or video file to text, kept with
    // the history (status "file").
    Q_INVOKABLE void transcribeFile(const QUrl& file);
    Q_INVOKABLE void cancelFile();
    Q_INVOKABLE QVariantList files() const;
    // Where quick notes go (Detta's): one folder each, Markdown plus the
    // recording and a screenshot, for people and local agents to read.
    Q_INVOKABLE QString notesDir() const;
    Q_INVOKABLE void openNotes() const;
    // Writes `text` to a file the user picked; "" or why not.
    Q_INVOKABLE QString saveText(const QUrl& file, const QString& text) const;
    QString fileStatus() const { return fileStatus_; }
    double fileProgress() const { return fileProgress_; }
    bool capturing() const { return capturing_; }

    Q_INVOKABLE QVariantList words() const;
    Q_INVOKABLE QString saveWord(qint64 id, const QString& word, const QString& misspelling);
    Q_INVOKABLE void removeWord(qint64 id);
    Q_INVOKABLE QVariantList snippetList() const;
    Q_INVOKABLE QString saveSnippet(qint64 id, const QString& trigger, const QString& expansion);
    Q_INVOKABLE void removeSnippet(qint64 id);

    Q_INVOKABLE QString setting(const QString& key, const QString& fallback = {}) const;
    Q_INVOKABLE void setSetting(const QString& key, const QString& value);
    Q_INVOKABLE void configureShortcuts();
    Q_INVOKABLE void openHub() { emit showHub(); }
    // An autostart entry in $XDG_CONFIG_HOME/autostart.
    Q_INVOKABLE bool autostart() const;
    Q_INVOKABLE void setAutostart(bool on);
    // What the pipeline makes of `text` with the current settings (Style page).
    // `category`: "personal", "work", "email", "other" (whose style to use).
    Q_INVOKABLE QString preview(const QString& text, const QString& category = QStringLiteral("other")) const;

signals:
    void fileChanged();
    void capturingChanged();
    void stateChanged();
    void levelChanged();
    void engineChanged();
    void triggersChanged();
    void problemChanged();
    void revisionChanged();
    void devicesChanged();
    // Something to tell the user near the bar ("Is your microphone muted?").
    void notice(const QString& text);
    void showHub();
    // The bar belongs on this screen (connector name): the one being typed on.
    void screenWanted(const QString& name);

private:
    enum class State { Idle, Recording, Processing };

    void onPressed(const QString& id);
    void onReleased(const QString& id, bool cancelled);
    void begin(bool locked);
    void finish();
    void discard(bool chime = true);
    void process(std::vector<float> pcm, std::int64_t retryOf = 0);
    void deliver(Dictation d, bool paste);
    void setState(State s);
    void setProblem(const QString& p);
    void bump();
    PipelineOptions options(Category c = Category::Other) const;
    QString saveNote(const Dictation& d, const std::vector<float>& pcm) const;
    QStringList hotwords() const;
    QString audioDir() const;
    int keepDays() const;
    void expireAudio();
    QString autostartPath() const;

    Store& store_;
    std::unique_ptr<Recorder> recorder_;
    std::unique_ptr<Inserter> inserter_;
    std::unique_ptr<Engine> engine_;
    std::unique_ptr<Shortcuts> shortcuts_;
    std::unique_ptr<Context> context_;
    std::unique_ptr<FileDecoder> decoder_;
    QString fileStatus_;
    double fileProgress_ = 0;
    int fileRun_ = 0;  // a cancelled file's late answer is dropped

    State state_ = State::Idle;
    bool locked_ = false;
    double level_ = 0;
    double peak_ = 0;  // loudest block of this recording
    QString problem_;
    int revision_ = 0;
    int run_ = 0;  // counts dictations, for timers that outlive one
    QString app_;  // where the dictation started
    Category category_ = Category::Other;
    QElapsedTimer held_;
    QTimer tapWindow_;  // after a quick tap: waiting for the second one
    QTimer noteWindow_;  // after the second: a third makes it a quick note
    bool note_ = false;  // this dictation is a quick note, not pasted
    QString shot_;       // its screenshot
    bool capturing_ = false;
    QString title_;      // the focused window's title when it began
    std::unique_ptr<Screenshot> screenshot_;
    QTimer limit_;      // 20 minutes, Flow's cap
    bool pttDown_ = false;

    // The field that got text last, for joining sentences.
    QString lastApp_;
    QString lastText_;
    QElapsedTimer lastTime_;
};

} // namespace tala
