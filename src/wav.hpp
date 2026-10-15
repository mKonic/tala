#pragma once
// 16 kHz mono 16-bit WAV, the shape tala keeps recordings in.

#include <QByteArray>
#include <QString>

#include <vector>

namespace tala {

// WAV (RIFF, 16-bit PCM, mono) for the given samples.
QByteArray wavBytes(const std::vector<float>& pcm, int rate = 16000);
// The reverse: false on anything but 16 kHz mono 16-bit PCM.
bool readWav(const QString& path, std::vector<float>& pcm);

} // namespace tala
