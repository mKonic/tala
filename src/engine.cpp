#include "engine.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <csignal>
#include <cstdint>
#include <sys/prctl.h>

namespace tala {

namespace {

constexpr int kDefaultIdleMinutes = 5;

// The child gets the signals tala blocks for its signalfd, and dies with tala.
void childSetup() {
    sigset_t all;
    sigfillset(&all);
    pthread_sigmask(SIG_UNBLOCK, &all, nullptr);
    prctl(PR_SET_PDEATHSIG, SIGTERM);
}

} // namespace

Engine::Engine(QObject* parent) : QObject(parent) {
    idle_.setSingleShot(true);
    idle_.setInterval(kDefaultIdleMinutes * 60 * 1000);
    connect(&idle_, &QTimer::timeout, this, [this] {
        // Nothing asked of it for a while: let the memory go.
        if (process_ && inFlight_.empty())
            process_->closeWriteChannel();  // tala-engine exits at the end of its input
    });
    state_ = QFileInfo::exists(dir() + "/model/model.fermion") ? State::Stopped : State::Missing;
    status_ = state_ == State::Stopped ? tr("Ready") : tr("Not downloaded yet");
}

Engine::~Engine() {
    for (QProcess* p : {process_, installer_})
        if (p) {
            p->disconnect(this);
            p->kill();
            p->waitForFinished(1000);
        }
}

QString Engine::dir() const {
    if (const QByteArray d = qgetenv("TALA_ENGINE_HOME"); !d.isEmpty())
        return QString::fromLocal8Bit(d);
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/tala/phonon";
}

QString Engine::program() const {
    // Beside tala (the build tree, /usr/bin), else on the PATH.
    const QString beside = QCoreApplication::applicationDirPath() + "/tala-engine";
    return QFileInfo(beside).isExecutable() ? beside : QStringLiteral("tala-engine");
}

void Engine::setIdleMinutes(int minutes) {
    idle_.setInterval(std::max(minutes, 0) * 60 * 1000);
    if (minutes <= 0)
        idle_.stop();
}

void Engine::setState(State s, const QString& status) {
    state_ = s;
    status_ = status;
    emit stateChanged();
}

void Engine::fail(const QString& why) {
    setState(State::Failed, why);
    auto waiting = std::move(waiting_);
    auto inFlight = std::move(inFlight_);
    waiting_.clear();
    inFlight_.clear();
    for (Request& r : waiting)
        r.done({false, {}, why, 0});
    for (Done& d : inFlight)
        d({false, {}, why, 0});
}

void Engine::start() {
    if (state_ == State::Installing || state_ == State::Starting || state_ == State::Ready)
        return;
    if (QFileInfo::exists(dir() + "/model/model.fermion") && QFileInfo::exists(dir() + "/kernels"))
        launch();
    else
        install();
}

void Engine::install() {
    setState(State::Installing, tr("Downloading the speech model"));
    installer_ = new QProcess(this);
    installer_->setChildProcessModifier(childSetup);
    connect(installer_, &QProcess::readyReadStandardOutput, this, [this] {
        while (installer_->canReadLine()) {
            const QJsonObject o = QJsonDocument::fromJson(installer_->readLine()).object();
            if (o.contains("total") && o["total"].toDouble() > 0) {
                const int pct = int(100 * o["done"].toDouble() / o["total"].toDouble());
                const QString what = o["stage"].toString() == "kernels" ? tr("speech kernels") : tr("speech model");
                setState(State::Installing, tr("Downloading the %1 (%2%)").arg(what).arg(pct));
            } else if (o.contains("error")) {
                status_ = o["error"].toString();
            }
        }
    });
    connect(installer_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        const QString why = status_;
        installer_->deleteLater();
        installer_ = nullptr;
        if (code != 0) {
            fail(tr("Couldn't download the speech model: %1").arg(why));
            return;
        }
        launch();
    });
    connect(installer_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            fail(tr("tala-engine is missing"));
    });
    installer_->start(program(), {"install", dir()});
}

void Engine::launch() {
    setState(State::Starting, tr("Loading the speech model"));
    buffer_.clear();
    process_ = new QProcess(this);
    process_->setChildProcessModifier(childSetup);
    process_->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(process_, &QProcess::readyReadStandardOutput, this, &Engine::onOutput);
    connect(process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        process_->deleteLater();
        process_ = nullptr;
        if (state_ == State::Ready && inFlight_.empty() && code == 0 && status == QProcess::NormalExit) {
            // Unloaded while idle: comes back with the next dictation.
            setState(State::Stopped, tr("Ready"));
            if (!waiting_.empty())
                start();
        } else if (state_ != State::Failed) {
            fail(tr("The speech engine stopped (exit %1)").arg(code));
        }
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            fail(tr("tala-engine is missing"));
    });
    process_->start(program(), {"serve", dir() + "/model", dir() + "/kernels"});
}

void Engine::onOutput() {
    buffer_ += process_->readAllStandardOutput();
    qsizetype nl;
    while ((nl = buffer_.indexOf('\n')) >= 0) {
        const QJsonObject o = QJsonDocument::fromJson(buffer_.left(nl)).object();
        buffer_.remove(0, nl + 1);
        if (state_ == State::Starting) {
            if (o["ready"].toBool()) {
                setState(State::Ready, tr("Ready"));
                auto waiting = std::move(waiting_);
                waiting_.clear();
                for (Request& r : waiting) {
                    send(r);
                    inFlight_.push_back(std::move(r.done));
                }
                if (inFlight_.empty() && idle_.interval() > 0)
                    idle_.start();
            } else {
                fail(tr("Couldn't load the speech model: %1").arg(o["error"].toString()));
            }
            continue;
        }
        if (inFlight_.empty())
            continue;
        Done done = std::move(inFlight_.front());
        inFlight_.pop_front();
        if (inFlight_.empty() && idle_.interval() > 0)
            idle_.start();
        done({true, o["text"].toString().trimmed(), {}, o["decode"].toDouble()});
    }
}

void Engine::send(const Request& r) {
    const QByteArray words = r.hotwords.mid(0, 25).join('\n').toUtf8();
    const auto samples = std::uint32_t(r.pcm.size()), wordBytes = std::uint32_t(words.size());
    QByteArray frame;
    frame.reserve(qsizetype(9 + words.size() + r.pcm.size() * 4));
    frame.append('T');
    frame.append(reinterpret_cast<const char*>(&samples), 4);
    frame.append(reinterpret_cast<const char*>(&wordBytes), 4);
    frame.append(words);
    frame.append(reinterpret_cast<const char*>(r.pcm.data()), qsizetype(r.pcm.size() * 4));
    process_->write(frame);
    idle_.stop();
}

void Engine::transcribe(const std::vector<float>& pcm, const QStringList& hotwords, Done done) {
    Request r{pcm, hotwords, std::move(done)};
    if (state_ == State::Ready && process_) {
        send(r);
        inFlight_.push_back(std::move(r.done));
        return;
    }
    waiting_.push_back(std::move(r));
    start();
}

} // namespace tala
