#include "recorder.hpp"

#include "engine.hpp"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/dict.h>

#include <QMetaObject>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace tala {

namespace {

constexpr int kChimeRate = 48000;

// Soft notes in Hz, each `ms` long, faded in and out.
std::vector<float> tone(std::initializer_list<double> notes, int ms, float gain = 0.10f) {
    std::vector<float> out;
    const int n = kChimeRate * ms / 1000;
    for (double hz : notes)
        for (int i = 0; i < n; ++i) {
            const double t = double(i) / kChimeRate;
            const double env = std::min({1.0, i / (0.004 * kChimeRate), (n - i) / (0.03 * kChimeRate)});
            out.push_back(gain * float(env * std::sin(2 * std::numbers::pi * hz * t)));
        }
    return out;
}

// Loudness for the bar's waveform: speech sits between about -50 and -15
// dBFS, so that span fills 0..1.
double loudness(double rms) {
    const double db = 20 * std::log10(std::max(rms, 1e-6));
    return std::clamp((db + 50) / 35, 0.0, 1.0);
}

const spa_pod* monoFloatFormat(spa_pod_builder* b, int rate) {
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = std::uint32_t(rate);
    info.channels = 1;
    info.position[0] = SPA_AUDIO_CHANNEL_MONO;
    return spa_format_audio_raw_build(b, SPA_PARAM_EnumFormat, &info);
}

} // namespace

struct Recorder::Listeners {
    spa_hook registry{};
    spa_hook stream{};
    spa_hook player{};
    pw_registry_events registryEvents{};
    pw_stream_events streamEvents{};
    pw_stream_events playerEvents{};
};

Recorder::Recorder(QObject* parent) : QObject(parent), listeners_(std::make_unique<Listeners>()) {
    pw_init(nullptr, nullptr);
    loop_ = pw_thread_loop_new("tala-audio", nullptr);
    if (!loop_)
        return;
    context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
    if (!context_ || pw_thread_loop_start(loop_) < 0) {
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
        return;
    }

    auto& L = *listeners_;
    L.registryEvents.version = PW_VERSION_REGISTRY_EVENTS;
    L.registryEvents.global = [](void* data, std::uint32_t id, std::uint32_t, const char* type, std::uint32_t,
                                 const spa_dict* props) {
        if (std::strcmp(type, PW_TYPE_INTERFACE_Node) != 0 || !props)
            return;
        const char* cls = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
        // Microphones, and the virtual ones filters make (noise suppression).
        if (!cls || (std::strcmp(cls, "Audio/Source") != 0 && std::strcmp(cls, "Audio/Source/Virtual") != 0))
            return;
        const char* name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
        const char* desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
        auto* self = static_cast<Recorder*>(data);
        {
            std::lock_guard lock(self->mutex_);
            self->nodes_.push_back({id, QString::fromUtf8(name ? name : ""),
                                    QString::fromUtf8(desc ? desc : (name ? name : ""))});
        }
        QMetaObject::invokeMethod(self, &Recorder::devicesChanged, Qt::QueuedConnection);
    };
    L.registryEvents.global_remove = [](void* data, std::uint32_t id) {
        auto* self = static_cast<Recorder*>(data);
        bool removed = false;
        {
            std::lock_guard lock(self->mutex_);
            removed = std::erase_if(self->nodes_, [id](const Node& n) { return n.id == id; }) > 0;
        }
        if (removed)
            QMetaObject::invokeMethod(self, &Recorder::devicesChanged, Qt::QueuedConnection);
    };

    L.streamEvents.version = PW_VERSION_STREAM_EVENTS;
    L.streamEvents.process = [](void* data) { static_cast<Recorder*>(data)->onProcess(); };
    L.streamEvents.state_changed = [](void* data, pw_stream_state, pw_stream_state state, const char*) {
        if (state == PW_STREAM_STATE_ERROR || state == PW_STREAM_STATE_UNCONNECTED)
            QMetaObject::invokeMethod(static_cast<Recorder*>(data), &Recorder::lost, Qt::QueuedConnection);
    };
    L.playerEvents.version = PW_VERSION_STREAM_EVENTS;
    L.playerEvents.process = [](void* data) { static_cast<Recorder*>(data)->onPlayback(); };

    pw_thread_loop_lock(loop_);
    core_ = pw_context_connect(context_, nullptr, 0);
    if (core_) {
        registry_ = pw_core_get_registry(core_, PW_VERSION_REGISTRY, 0);
        pw_registry_add_listener(registry_, &L.registry, &L.registryEvents, this);
    }
    pw_thread_loop_unlock(loop_);
    if (!core_) {
        pw_thread_loop_stop(loop_);
        pw_context_destroy(context_);
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
    }
}

Recorder::~Recorder() {
    if (!loop_)
        return;
    pw_thread_loop_lock(loop_);
    if (stream_)
        pw_stream_destroy(stream_);
    if (player_)
        pw_stream_destroy(player_);
    if (registry_)
        pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry_));
    pw_core_disconnect(core_);
    pw_thread_loop_unlock(loop_);
    pw_thread_loop_stop(loop_);
    pw_context_destroy(context_);
    pw_thread_loop_destroy(loop_);
}

bool Recorder::start(const QString& device) {
    // Tests: a WAV file stands in for the microphone, played in real time.
    if (const QByteArray fake = qgetenv("TALA_FAKE_MIC"); !fake.isEmpty()) {
        std::vector<float> pcm;
        if (!readWav(QString::fromLocal8Bit(fake), pcm))
            return false;
        fake_ = std::make_unique<QTimer>();
        auto at = std::make_shared<std::size_t>(0);
        {
            std::lock_guard lock(mutex_);
            samples_.clear();
        }
        QObject::connect(fake_.get(), &QTimer::timeout, this, [this, pcm = std::move(pcm), at] {
            const std::size_t n = std::min<std::size_t>(kRate / 50, pcm.size() - std::min(*at, pcm.size()));
            double sum = 0;
            for (std::size_t i = 0; i < n; ++i)
                sum += double(pcm[*at + i]) * pcm[*at + i];
            {
                std::lock_guard lock(mutex_);
                samples_.insert(samples_.end(), pcm.begin() + long(*at), pcm.begin() + long(*at + n));
                // Past the end: silence, as a quiet room would give.
                if (n == 0)
                    samples_.insert(samples_.end(), kRate / 50, 0.0f);
            }
            *at += n;
            emit level(loudness(n ? std::sqrt(sum / double(n)) : 0.0));
        });
        fake_->start(20);
        return true;
    }
    if (!loop_ || stream_)
        return false;
    {
        std::lock_guard lock(mutex_);
        samples_.clear();
        samples_.reserve(kRate * 60);
    }
    pw_thread_loop_lock(loop_);
    pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
                                             PW_KEY_MEDIA_ROLE, "Communication", PW_KEY_APP_NAME, "tala",
                                             PW_KEY_NODE_NAME, "tala-dictation", nullptr);
    if (!device.isEmpty())
        pw_properties_set(props, PW_KEY_TARGET_OBJECT, device.toUtf8().constData());
    // A microphone unplugged mid-dictation ends it (lost()), with what it
    // heard, instead of carrying on from another one.
    pw_properties_set(props, PW_KEY_NODE_DONT_RECONNECT, "true");
    stream_ = pw_stream_new(core_, "Dictation", props);
    bool ok = stream_ != nullptr;
    if (ok) {
        pw_stream_add_listener(stream_, &listeners_->stream, &listeners_->streamEvents, this);
        std::uint8_t buf[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
        const spa_pod* params[] = {monoFloatFormat(&b, kRate)};
        ok = pw_stream_connect(stream_, PW_DIRECTION_INPUT, PW_ID_ANY,
                               pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                               PW_STREAM_FLAG_RT_PROCESS),
                               params, 1) == 0;
        if (!ok) {
            spa_hook_remove(&listeners_->stream);
            pw_stream_destroy(stream_);
            stream_ = nullptr;
        }
    }
    pw_thread_loop_unlock(loop_);
    return ok;
}

std::vector<float> Recorder::stop() {
    fake_.reset();
    if (loop_ && stream_) {
        pw_thread_loop_lock(loop_);
        spa_hook_remove(&listeners_->stream);
        pw_stream_destroy(stream_);
        stream_ = nullptr;
        pw_thread_loop_unlock(loop_);
    }
    std::lock_guard lock(mutex_);
    return std::exchange(samples_, {});
}

double Recorder::seconds() const {
    std::lock_guard lock(mutex_);
    return double(samples_.size()) / kRate;
}

void Recorder::onProcess() {
    pw_buffer* b = pw_stream_dequeue_buffer(stream_);
    if (!b)
        return;
    const spa_data& d = b->buffer->datas[0];
    if (d.data && d.chunk) {
        const auto* p = reinterpret_cast<const float*>(static_cast<const std::uint8_t*>(d.data) + d.chunk->offset);
        const std::size_t n = d.chunk->size / sizeof(float);
        double sum = 0;
        for (std::size_t i = 0; i < n; ++i)
            sum += double(p[i]) * p[i];
        {
            std::lock_guard lock(mutex_);
            samples_.insert(samples_.end(), p, p + n);
        }
        const double v = loudness(n ? std::sqrt(sum / double(n)) : 0);
        if (levelPending_.exchange(1) == 0)
            QMetaObject::invokeMethod(this, [this, v] {
                levelPending_ = 0;
                emit level(v);
            }, Qt::QueuedConnection);
    }
    pw_stream_queue_buffer(stream_, b);
}

bool Recorder::has(const QString& device) const {
    std::lock_guard lock(mutex_);
    return std::ranges::any_of(nodes_, [&](const Node& n) { return n.name == device; });
}

QVariantList Recorder::devices() const {
    QVariantList out;
    std::lock_guard lock(mutex_);
    for (const Node& n : nodes_)
        out.append(QVariantMap{{"name", n.name}, {"description", n.description}});
    return out;
}

void Recorder::chime(Chime c) {
    if (!loop_)
        return;
    std::vector<float> t;
    switch (c) {
    case Chime::Start: t = tone({587.33, 880.0}, 70); break;
    case Chime::Stop: t = tone({880.0, 587.33}, 70); break;
    case Chime::Cancel: t = tone({440.0}, 90, 0.07f); break;
    case Chime::Error: t = tone({311.13, 311.13}, 110); break;
    }
    pw_thread_loop_lock(loop_);
    {
        std::lock_guard lock(mutex_);
        tone_ = std::move(t);
        toneAt_ = 0;
    }
    if (!player_) {
        pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
                                                 PW_KEY_MEDIA_ROLE, "Notification", PW_KEY_APP_NAME, "tala", nullptr);
        player_ = pw_stream_new(core_, "tala chime", props);
        if (player_) {
            pw_stream_add_listener(player_, &listeners_->player, &listeners_->playerEvents, this);
            std::uint8_t buf[1024];
            spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
            const spa_pod* params[] = {monoFloatFormat(&b, kChimeRate)};
            pw_stream_connect(player_, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                              pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                              PW_STREAM_FLAG_RT_PROCESS),
                              params, 1);
        }
    }
    pw_thread_loop_unlock(loop_);
}

void Recorder::onPlayback() {
    pw_buffer* b = pw_stream_dequeue_buffer(player_);
    if (!b)
        return;
    spa_data& d = b->buffer->datas[0];
    if (!d.data)
        return;
    auto* out = static_cast<float*>(d.data);
    std::size_t n = d.maxsize / sizeof(float);
    if (b->requested)
        n = std::min<std::size_t>(n, b->requested);
    bool done = false;
    {
        std::lock_guard lock(mutex_);
        for (std::size_t i = 0; i < n; ++i)
            out[i] = toneAt_ < tone_.size() ? tone_[toneAt_++] : 0.0f;
        done = toneAt_ >= tone_.size();
    }
    d.chunk->offset = 0;
    d.chunk->stride = sizeof(float);
    d.chunk->size = std::uint32_t(n * sizeof(float));
    pw_stream_queue_buffer(player_, b);
    // Silence left playing keeps the sink awake: let it go a moment later.
    if (done)
        QMetaObject::invokeMethod(this, [this] {
            QTimer::singleShot(300, this, [this] {
                pw_thread_loop_lock(loop_);
                bool idle;
                {
                    std::lock_guard lock(mutex_);
                    idle = toneAt_ >= tone_.size();
                }
                if (player_ && idle) {
                    spa_hook_remove(&listeners_->player);
                    pw_stream_destroy(player_);
                    player_ = nullptr;
                }
                pw_thread_loop_unlock(loop_);
            });
        }, Qt::QueuedConnection);
}

} // namespace tala
