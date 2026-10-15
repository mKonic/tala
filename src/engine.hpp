#pragma once
// Speech to text: Phonon-2 in tala-engine, a process of its own.
//
// The model loads in about 0.15 s, so it is started when a dictation starts
// (the recording outlasts the load) and stopped after a few idle minutes:
// between dictations it holds no memory at all. On first use tala-engine
// downloads the model and Fermion's kernels into tala's folder.

#include "wav.hpp"

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

#include <deque>
#include <functional>
#include <vector>

namespace tala {

struct Transcript {
    bool ok = false;
    QString text;
    QString error;
    double decodeSeconds = 0;
};

class Engine : public QObject {
    Q_OBJECT

public:
    using Done = std::function<void(const Transcript&)>;
    enum class State { Missing, Installing, Stopped, Starting, Ready, Failed };
    Q_ENUM(State)

    explicit Engine(QObject* parent = nullptr);
    ~Engine() override;

    // Brings the engine up, installing it first if needed. Idempotent.
    void start();
    // 16 kHz mono samples in [-1, 1]. `done` runs on the GUI thread.
    void transcribe(const std::vector<float>& pcm, const QStringList& hotwords, Done done);
    // How long an idle engine stays loaded (0: until tala quits).
    void setIdleMinutes(int minutes);

    State state() const { return state_; }
    // A line for the Hub: what it's doing or why it failed.
    QString status() const { return status_; }

signals:
    void stateChanged();

private:
    struct Request {
        std::vector<float> pcm;
        QStringList hotwords;
        Done done;
    };

    void setState(State s, const QString& status);
    void install();
    void launch();
    void send(const Request& r);
    void onOutput();
    void fail(const QString& why);
    QString dir() const;
    QString program() const;

    State state_ = State::Stopped;
    QString status_;
    QProcess* process_ = nullptr;
    QProcess* installer_ = nullptr;
    QByteArray buffer_;
    std::deque<Request> waiting_;  // asked for before the engine was up
    std::deque<Done> inFlight_;    // sent, answers come back in order
    QTimer idle_;
};

} // namespace tala
