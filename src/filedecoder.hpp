#pragma once
// An audio or video file as 16 kHz mono samples, for transcribing a
// recording (Detta's file transcription). Qt Multimedia decodes it (its
// FFmpeg backend reads what FFmpeg reads) and resamples it on the way.

#include <QAudioDecoder>
#include <QObject>
#include <QString>

#include <memory>
#include <vector>

namespace tala {

class FileDecoder : public QObject {
    Q_OBJECT

public:
    explicit FileDecoder(QObject* parent = nullptr);

    void decode(const QString& path);
    void cancel();
    bool busy() const { return decoder_ != nullptr; }

signals:
    // 0..1, as far as the file's duration is known.
    void progress(double done);
    void finished(const std::vector<float>& pcm);
    void failed(const QString& why);

private:
    std::unique_ptr<QAudioDecoder> decoder_;
    std::vector<float> pcm_;
};

} // namespace tala
