#include "wav.hpp"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tala {

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

} // namespace tala
