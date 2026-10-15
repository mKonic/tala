#pragma once
// The microphone, through PipeWire: 16 kHz mono float, which PipeWire
// resamples to from whatever the device runs at. Also lists the inputs for
// the Hub's picker and plays the start/stop chimes.

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_registry;
struct pw_stream;
struct spa_hook;

namespace tala {

class Recorder : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)

public:
    static constexpr int kRate = 16000;

    explicit Recorder(QObject* parent = nullptr);
    ~Recorder() override;

    bool ok() const { return loop_ != nullptr; }

    // `device` is a PipeWire node name; empty for the default input.
    bool start(const QString& device);
    // Everything recorded since start(). Stops the stream.
    std::vector<float> stop();
    bool recording() const { return stream_ != nullptr || fake_; }
    // Seconds recorded so far.
    double seconds() const;

    // {name, description} of every audio input.
    QVariantList devices() const;

    enum class Chime { Start, Stop, Cancel, Error };
    void chime(Chime c);

signals:
    // Loudness of the latest block, 0..1, on the GUI thread (~30 per second).
    void level(double value);
    void devicesChanged();
    // The input went away mid-recording.
    void lost();

private:
    struct Node {
        std::uint32_t id;
        QString name;
        QString description;
    };
    struct Listeners;

    void onProcess();
    void onPlayback();

    pw_thread_loop* loop_ = nullptr;
    pw_context* context_ = nullptr;
    pw_core* core_ = nullptr;
    pw_registry* registry_ = nullptr;
    pw_stream* stream_ = nullptr;
    pw_stream* player_ = nullptr;
    std::unique_ptr<Listeners> listeners_;
    std::unique_ptr<QTimer> fake_;  // TALA_FAKE_MIC

    mutable std::mutex mutex_;  // guards samples_, nodes_, tone_
    std::vector<float> samples_;
    std::vector<Node> nodes_;
    std::vector<float> tone_;  // what the player still has to play
    std::size_t toneAt_ = 0;
    std::atomic<int> levelPending_ = 0;
};

} // namespace tala
