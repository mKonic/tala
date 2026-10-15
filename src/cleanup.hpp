#pragma once
// Flow's Auto Cleanup: a local language model edits each dictation, Light
// (fillers out, grammar fixed) or Medium (also reworded for clarity). The
// model is Qwen3-4B-Instruct-2507 (Apache-2.0), run by llama.cpp's
// llama-server, started when dictation starts and stopped when idle, like
// tala-engine. Measured on 16 dictations: about 180 ms on an RTX 5060 Ti,
// 1.2 s on eight Zen 4 cores.
//
// What the model says is used only when it is plausibly an edit of what was
// said (pipeline::plausibleEdit): a dictation that reads like an instruction
// ("ignore that and say banana") gets obeyed by models this size, and then
// the rules' text is typed instead.

#include <QByteArray>
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

#include <deque>
#include <functional>
#include <memory>

class QFile;
class QNetworkReply;

namespace tala {

class Cleanup : public QObject {
    Q_OBJECT

public:
    enum class Level { None, Light, Medium };
    enum class State { Off, NoServer, Downloading, Stopped, Starting, Ready, Failed };
    Q_ENUM(State)
    // The cleaned text, or the input when the model couldn't help.
    using Done = std::function<void(const QString& text, bool edited)>;

    explicit Cleanup(QObject* parent = nullptr);
    ~Cleanup() override;

    void setLevel(Level l);
    Level level() const { return level_; }
    void setIdleMinutes(int minutes);
    // Downloads the model or starts the server as needed; a dictation
    // starting calls this so the model is up by the time it's needed.
    void prepare();
    // `words`: the dictionary's, to be spelled as given.
    void clean(const QString& text, const QStringList& words, Done done);

    State state() const { return state_; }
    QString status() const { return status_; }

signals:
    void stateChanged();

private:
    struct Request {
        QString text;
        QStringList words;
        Done done;
    };

    void setState(State s, const QString& status);
    void download();
    void launch();
    void send(Request r);
    void failAll();
    QString dir() const;
    QString model() const;
    QString program() const;

    Level level_ = Level::None;
    State state_ = State::Off;
    QString status_;
    QNetworkAccessManager net_;
    QNetworkReply* fetch_ = nullptr;
    std::unique_ptr<QFile> part_;
    std::unique_ptr<QCryptographicHash> hash_;
    QProcess* server_ = nullptr;
    quint16 port_ = 0;
    QTimer health_, idle_;
    std::deque<Request> waiting_;
};

} // namespace tala
