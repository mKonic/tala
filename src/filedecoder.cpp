#include "filedecoder.hpp"

#include <QAudioBuffer>
#include <QAudioFormat>
#include <QUrl>

namespace tala {

FileDecoder::FileDecoder(QObject* parent) : QObject(parent) {}

void FileDecoder::decode(const QString& path) {
    cancel();
    pcm_.clear();
    decoder_ = std::make_unique<QAudioDecoder>();
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Float);
    decoder_->setAudioFormat(format);
    decoder_->setSource(QUrl::fromLocalFile(path));
    QAudioDecoder* d = decoder_.get();
    connect(d, &QAudioDecoder::bufferReady, this, [this, d] {
        const QAudioBuffer b = d->read();
        if (!b.isValid())
            return;
        const float* data = b.constData<float>();
        const qsizetype channels = std::max(1, b.format().channelCount());
        const qsizetype frames = b.frameCount();
        pcm_.reserve(pcm_.size() + std::size_t(frames));
        // Mono already, unless the backend kept the channels: then the mean.
        for (qsizetype f = 0; f < frames; ++f) {
            float sum = 0;
            for (qsizetype c = 0; c < channels; ++c)
                sum += data[f * channels + c];
            pcm_.push_back(sum / float(channels));
        }
        if (d->duration() > 0)
            emit progress(std::min(1.0, double(d->position()) / double(d->duration())));
    });
    connect(d, &QAudioDecoder::finished, this, [this] {
        std::vector<float> pcm = std::move(pcm_);
        pcm_.clear();
        decoder_.release()->deleteLater();
        emit finished(pcm);
    });
    connect(d, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, [this, d](QAudioDecoder::Error) {
        const QString why = d->errorString();
        decoder_.release()->deleteLater();
        pcm_.clear();
        emit failed(why.isEmpty() ? tr("Couldn't read the file") : why);
    });
    d->start();
}

void FileDecoder::cancel() {
    if (!decoder_)
        return;
    decoder_->disconnect(this);
    decoder_->stop();
    decoder_.release()->deleteLater();
    pcm_.clear();
}

} // namespace tala
