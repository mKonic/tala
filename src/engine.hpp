#pragma once
// Speech to text. One interface, so the model's runtime can change under
// the app: today the Phonon-2 server (`fermion serve` on a private Unix
// socket, installed into tala's own folder on first use).

#include <QObject>
#include <QProcess>
#include <QStringList>

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

    enum class State { Missing, Installing, Starting, Ready, Failed };
    Q_ENUM(State)

    explicit Engine(QObject* parent = nullptr) : QObject(parent) {}

    // Brings the engine up (installing it if it isn't there). Idempotent.
    virtual void start() = 0;
    // 16 kHz mono samples in [-1, 1]. `done` runs on the GUI thread.
    virtual void transcribe(const std::vector<float>& pcm, const QStringList& hotwords, Done done) = 0;
    virtual State state() const = 0;
    // A line for the Hub: what it's doing or why it failed.
    virtual QString status() const = 0;

signals:
    void stateChanged();
};

// `fermion serve phonon-2 --unix-socket` as a child process.
class ServerEngine : public Engine {
    Q_OBJECT

public:
    explicit ServerEngine(QObject* parent = nullptr);
    ~ServerEngine() override;

    void start() override;
    void transcribe(const std::vector<float>& pcm, const QStringList& hotwords, Done done) override;
    State state() const override { return state_; }
    QString status() const override { return status_; }

private:
    void setState(State s, const QString& status);
    void install();
    void launch();
    void waitForSocket(int tries);
    QString home() const;
    QString fermion() const;
    QString socketPath() const;

    State state_ = State::Missing;
    QString status_;
    QProcess* server_ = nullptr;
    QProcess* installer_ = nullptr;
    // Asked for before the server was up.
    struct Pending {
        std::vector<float> pcm;
        QStringList hotwords;
        Done done;
    };
    std::vector<Pending> pending_;
};

// WAV (RIFF, 16-bit PCM, mono) for the given samples.
QByteArray wavBytes(const std::vector<float>& pcm, int rate = 16000);
// The reverse, for a saved recording: false on anything but tala's own shape.
bool readWav(const QString& path, std::vector<float>& pcm);

} // namespace tala
