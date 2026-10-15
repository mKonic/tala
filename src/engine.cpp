#include "engine.hpp"

#include <QDir>
#include <QtEndian>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstring>
#include <sys/prctl.h>

namespace tala {

namespace {

// The Python side, into tala's folder: PyTorch's CPU build first (the
// default one drags in CUDA), then the engine. uv when it's there, since it
// brings its own Python 3.12; the system's otherwise.
constexpr const char* kInstallScript = R"sh(
set -e
dir="$1"
# In place (a venv can't move: its scripts name their own path), with a
# stamp at the end so a half-done install is redone.
rm -rf "$dir"
if command -v uv >/dev/null 2>&1; then
    uv venv -q -p 3.12 "$dir"
    uv pip install -q --python "$dir/bin/python" --no-deps torch --index-url https://download.pytorch.org/whl/cpu
    uv pip install -q --python "$dir/bin/python" fermion-research
else
    py=""
    for v in python3.12 python3.13 python3.11 python3.10; do
        if command -v $v >/dev/null 2>&1; then py=$v; break; fi
    done
    [ -n "$py" ] || { echo "needs uv, or Python 3.10 to 3.13" >&2; exit 3; }
    "$py" -m venv "$dir"
    "$dir/bin/pip" install -q --no-deps torch --index-url https://download.pytorch.org/whl/cpu
    "$dir/bin/pip" install -q fermion-research
fi
# The audio argument is required even here; it isn't read.
"$dir/bin/fermion" transcribe --download-only phonon-2 none.wav >/dev/null
touch "$dir/.tala-ready"
)sh";

// One POST over the server's socket. HTTP/1.1 with Connection: close, so
// the reply is everything up to the hang-up.
void post(const QString& socket, const QByteArray& path, const QByteArray& contentType, const QByteArray& body,
          QObject* context, std::function<void(int, QByteArray, QString)> done) {
    auto* s = new QLocalSocket(context);
    auto reply = std::make_shared<QByteArray>();
    auto finished = std::make_shared<bool>(false);
    auto finish = [s, reply, finished, done](const QString& error) {
        if (std::exchange(*finished, true))
            return;
        s->deleteLater();
        if (!error.isEmpty())
            return done(0, {}, error);
        const qsizetype split = reply->indexOf("\r\n\r\n");
        if (split < 0)
            return done(0, {}, QStringLiteral("the engine gave no answer"));
        const QList<QByteArray> status = reply->left(reply->indexOf("\r\n")).split(' ');
        done(status.size() > 1 ? status[1].toInt() : 0, reply->mid(split + 4), {});
    };
    QObject::connect(s, &QLocalSocket::connected, s, [s, path, contentType, body] {
        QByteArray head = "POST " + path + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n";
        head += "Content-Type: " + contentType + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
        s->write(head);
        s->write(body);
    });
    QObject::connect(s, &QLocalSocket::readyRead, s, [s, reply] { reply->append(s->readAll()); });
    QObject::connect(s, &QLocalSocket::disconnected, s, [s, reply, finish] {
        reply->append(s->readAll());
        finish({});
    });
    QObject::connect(s, &QLocalSocket::errorOccurred, s, [s, finish](QLocalSocket::LocalSocketError e) {
        if (e != QLocalSocket::PeerClosedError)
            finish(s->errorString());
    });
    s->connectToServer(socket);
}

} // namespace

QByteArray wavBytes(const std::vector<float>& pcm, int rate) {
    QByteArray out;
    const auto bytes = std::uint32_t(pcm.size() * 2);
    auto u32 = [&out](std::uint32_t v) { out.append(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&out](std::uint16_t v) { out.append(reinterpret_cast<const char*>(&v), 2); };
    out.reserve(qsizetype(44 + bytes));
    out.append("RIFF");
    u32(36 + bytes);
    out.append("WAVEfmt ");
    u32(16);
    u16(1);  // PCM
    u16(1);  // mono
    u32(std::uint32_t(rate));
    u32(std::uint32_t(rate * 2));
    u16(2);
    u16(16);
    out.append("data");
    u32(bytes);
    for (float f : pcm) {
        const auto s = std::int16_t(std::lround(std::clamp(f, -1.0f, 1.0f) * 32767.0f));
        out.append(reinterpret_cast<const char*>(&s), 2);
    }
    return out;
}

bool readWav(const QString& path, std::vector<float>& pcm) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray d = f.readAll();
    if (d.size() < 12 || !d.startsWith("RIFF") || d.mid(8, 4) != "WAVE")
        return false;
    const auto* p = reinterpret_cast<const uchar*>(d.constData());
    // Chunks in any order (ffmpeg puts a LIST before the data).
    bool format = false;
    for (qsizetype at = 12; at + 8 <= d.size();) {
        const QByteArray id = d.mid(at, 4);
        const qsizetype size = qFromLittleEndian<quint32>(p + at + 4);
        const qsizetype body = at + 8;
        if (body + size > d.size() && id != "data")
            return false;
        if (id == "fmt ") {
            // 16-bit PCM, mono, 16 kHz: what tala itself writes.
            format = size >= 16 && qFromLittleEndian<quint16>(p + body) == 1 &&
                     qFromLittleEndian<quint16>(p + body + 2) == 1 &&
                     qFromLittleEndian<quint32>(p + body + 4) == 16000 &&
                     qFromLittleEndian<quint16>(p + body + 14) == 16;
        } else if (id == "data") {
            if (!format)
                return false;
            const qsizetype n = std::min(size, d.size() - body) / 2;
            pcm.resize(std::size_t(n));
            for (qsizetype i = 0; i < n; ++i)
                pcm[std::size_t(i)] = float(qFromLittleEndian<qint16>(p + body + i * 2)) / 32768.0f;
            return true;
        }
        at = body + size + (size & 1);
    }
    return false;
}

ServerEngine::ServerEngine(QObject* parent) : Engine(parent) {}

ServerEngine::~ServerEngine() {
    if (server_) {
        server_->disconnect(this);
        server_->terminate();
        if (!server_->waitForFinished(2000))
            server_->kill();
    }
    if (installer_) {
        installer_->disconnect(this);
        installer_->kill();
        installer_->waitForFinished(1000);
    }
}

QString ServerEngine::home() const {
    if (const QByteArray dir = qgetenv("TALA_ENGINE_HOME"); !dir.isEmpty())
        return QString::fromLocal8Bit(dir);
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/tala/engine";
}

QString ServerEngine::fermion() const {
    return home() + "/bin/fermion";
}

QString ServerEngine::socketPath() const {
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty())
        runtime = QDir::tempPath();
    return runtime + "/tala-phonon.sock";
}

void ServerEngine::setState(State s, const QString& status) {
    state_ = s;
    status_ = status;
    if (s == State::Failed) {
        auto pending = std::move(pending_);
        pending_.clear();
        for (Pending& p : pending)
            p.done({false, {}, status, 0});
    }
    emit stateChanged();
}

void ServerEngine::start() {
    if (state_ == State::Installing || state_ == State::Starting || state_ == State::Ready)
        return;
    if (QFileInfo::exists(home() + "/.tala-ready"))
        launch();
    else
        install();
}

void ServerEngine::install() {
    setState(State::Installing, tr("Installing the speech model (about 900 MB, once)"));
    QDir().mkpath(QFileInfo(home()).path());
    installer_ = new QProcess(this);
    installer_->setProcessChannelMode(QProcess::MergedChannels);
    connect(installer_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        const QString out = QString::fromUtf8(installer_->readAll()).trimmed();
        installer_->deleteLater();
        installer_ = nullptr;
        if (code != 0 || !QFileInfo::exists(home() + "/.tala-ready")) {
            setState(State::Failed, tr("Couldn't install the speech model: %1").arg(out.section('\n', -1)));
            return;
        }
        launch();
    });
    installer_->setChildProcessModifier([] {
        sigset_t all;
        sigfillset(&all);
        pthread_sigmask(SIG_UNBLOCK, &all, nullptr);
    });
    installer_->start("sh", {"-c", kInstallScript, "sh", home()});
}

void ServerEngine::launch() {
    setState(State::Starting, tr("Loading the speech model"));
    QFile::remove(socketPath());
    server_ = new QProcess(this);
    server_->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    server_->setStandardOutputFile(QProcess::nullDevice());
    connect(server_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        server_->deleteLater();
        server_ = nullptr;
        setState(State::Failed, tr("The speech engine stopped (exit %1)").arg(code));
    });
    // Never outlives tala, however tala ends.
    server_->setChildProcessModifier([] {
        // tala blocks these to read them from a signalfd; the child gets them back.
        sigset_t all;
        sigfillset(&all);
        pthread_sigmask(SIG_UNBLOCK, &all, nullptr);
        prctl(PR_SET_PDEATHSIG, SIGTERM);
    });
    server_->start(fermion(), {"serve", "phonon-2", "--unix-socket", socketPath()});
    waitForSocket(600);  // first start unpacks the model
}

void ServerEngine::waitForSocket(int tries) {
    if (!server_)
        return;
    if (QFileInfo::exists(socketPath())) {
        setState(State::Ready, tr("Ready"));
        auto pending = std::move(pending_);
        pending_.clear();
        for (Pending& p : pending)
            transcribe(p.pcm, p.hotwords, std::move(p.done));
        return;
    }
    if (tries <= 0) {
        setState(State::Failed, tr("The speech engine didn't start"));
        return;
    }
    QTimer::singleShot(100, this, [this, tries] { waitForSocket(tries - 1); });
}

void ServerEngine::transcribe(const std::vector<float>& pcm, const QStringList& hotwords, Done done) {
    if (state_ != State::Ready) {
        pending_.push_back({pcm, hotwords, std::move(done)});
        start();
        return;
    }
    const QByteArray boundary = "tala" + QUuid::createUuid().toByteArray(QUuid::Id128);
    QByteArray body;
    auto field = [&](const QByteArray& name, const QByteArray& value) {
        body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + name + "\"\r\n\r\n" + value + "\r\n";
    };
    field("model", "phonon-2");
    field("response_format", "verbose_json");
    if (!hotwords.isEmpty()) {
        QJsonArray list;
        for (const QString& w : hotwords.mid(0, 25))
            list.append(w);
        field("hotwords", QJsonDocument(list).toJson(QJsonDocument::Compact));
    }
    body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"dictation.wav\"\r\n";
    body += "Content-Type: audio/wav\r\n\r\n" + wavBytes(pcm) + "\r\n--" + boundary + "--\r\n";

    post(socketPath(), "/v1/audio/transcriptions", "multipart/form-data; boundary=" + boundary, body, this,
         [done = std::move(done)](int status, const QByteArray& reply, const QString& error) {
             if (!error.isEmpty())
                 return done({false, {}, error, 0});
             const QJsonObject o = QJsonDocument::fromJson(reply).object();
             if (status != 200)
                 return done({false, {}, o["error"].toObject()["message"].toString(QStringLiteral("HTTP %1").arg(status)), 0});
             done({true, o["text"].toString().trimmed(), {}, o["x_fermion"].toObject()["decode_seconds"].toDouble()});
         });
}

} // namespace tala
