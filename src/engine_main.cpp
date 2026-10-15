// tala-engine: Phonon-2 in its own process, so the app never carries the
// model's memory into a crash and a crash in the kernels never takes the app.
//
//   tala-engine install DIR          model/ and kernels/ into DIR
//   tala-engine transcribe MODEL_DIR LIB_DIR FILE.wav [WORD,WORD...]
//   tala-engine serve MODEL_DIR LIB_DIR
//
// serve speaks over stdin/stdout. Once loaded it writes one JSON line,
// {"ready":true,...} or {"error":"..."}. Each request is binary:
//   'T', u32 samples, u32 hotword bytes, hotwords (UTF-8, one per line),
//   float32 samples (16 kHz mono)
// and its answer one JSON line: {"text":"...","decode":0.08}.
// install writes {"stage":"model","done":N,"total":N} lines as it goes, then
// {"installed":true} or {"error":"..."}.

#include "phonon/install.hpp"
#include "phonon/phonon.hpp"
#include "wav.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

using namespace tala;

namespace {

void line(const QJsonObject& o) {
    const QByteArray b = QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(b.data(), 1, std::size_t(b.size()), stdout);
    std::fflush(stdout);
}

bool readAll(void* buf, std::size_t n) {
    auto* p = static_cast<char*>(buf);
    while (n) {
        const ssize_t r = ::read(STDIN_FILENO, p, n);
        if (r <= 0)
            return false;
        p += r;
        n -= std::size_t(r);
    }
    return true;
}

std::vector<std::string> splitLines(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            if (!cur.empty())
                out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

int usage() {
    std::fprintf(stderr, "usage: tala-engine install DIR\n"
                         "       tala-engine transcribe MODEL_DIR LIB_DIR FILE.wav [WORD,WORD...]\n"
                         "       tala-engine serve MODEL_DIR LIB_DIR\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "install") {
        std::string error;
        long long last = -1;
        const bool ok = phonon::install(argv[2], [&](const std::string& stage, long long done, long long total) {
            // A line per percent is plenty.
            const long long pct = total ? done * 100 / total : 0;
            if (pct == last)
                return;
            last = pct;
            line({{"stage", QString::fromStdString(stage)}, {"done", done}, {"total", total}});
        }, error);
        line(ok ? QJsonObject{{"installed", true}} : QJsonObject{{"error", QString::fromStdString(error)}});
        return ok ? 0 : 1;
    }
    if (argc < 4)
        return usage();
    const std::string cmd = argv[1], modelDir = argv[2], libDir = argv[3];
    const int threads = std::getenv("TALA_THREADS") ? std::atoi(std::getenv("TALA_THREADS")) : 0;

    const auto t0 = std::chrono::steady_clock::now();
    phonon::Model model;
    std::string error;
    const bool loaded = model.load(modelDir, libDir, threads, error);
    const double loadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    if (cmd == "transcribe") {
        if (argc < 5)
            return usage();
        if (!loaded) {
            std::fprintf(stderr, "tala-engine: %s\n", error.c_str());
            return 1;
        }
        std::vector<float> pcm;
        if (!readWav(QString::fromLocal8Bit(argv[4]), pcm)) {
            std::fprintf(stderr, "tala-engine: %s isn't 16 kHz mono 16-bit WAV\n", argv[4]);
            return 1;
        }
        const auto words = argc > 5 ? splitLines(argv[5], ',') : std::vector<std::string>{};
        const phonon::Result r = model.transcribe(pcm.data(), pcm.size(), words);
        std::printf("%s\n", r.text.c_str());
        std::fprintf(stderr, "load %.2f s, %.1f s of audio in %.3f s (%d threads, %s)\n", loadSeconds, r.audioSeconds,
                     r.decodeSeconds, model.threads(), model.library().c_str());
        return 0;
    }
    if (cmd != "serve")
        return usage();

    if (!loaded) {
        line({{"error", QString::fromStdString(error)}});
        return 1;
    }
    line({{"ready", true},
          {"threads", model.threads()},
          {"library", QString::fromStdString(model.library())},
          {"load", loadSeconds}});
    for (;;) {
        char kind = 0;
        std::uint32_t samples = 0, wordBytes = 0;
        if (!readAll(&kind, 1) || kind != 'T' || !readAll(&samples, 4) || !readAll(&wordBytes, 4))
            return 0;  // tala closed the pipe
        std::string words(wordBytes, '\0');
        std::vector<float> pcm(samples);
        if (!readAll(words.data(), wordBytes) || !readAll(pcm.data(), std::size_t(samples) * sizeof(float)))
            return 0;
        const phonon::Result r = model.transcribe(pcm.data(), pcm.size(), splitLines(words, '\n'));
        line({{"text", QString::fromStdString(r.text)}, {"decode", r.decodeSeconds}});
    }
}
