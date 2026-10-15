#include "mel.hpp"

#include <cmath>
#include <complex>
#include <numbers>

namespace tala::phonon {

namespace {

constexpr int kFft = 512, kHop = 160, kWin = 400, kBins = kFft / 2 + 1;
constexpr float kPreemph = 0.97f;
constexpr double kLogGuard = 1.0 / (1 << 24);  // 2^-24

// librosa's Slaney mel scale.
double hzToMel(double f) {
    constexpr double fsp = 200.0 / 3, minLogHz = 1000.0, minLogMel = minLogHz / fsp;
    const double logstep = std::log(6.4) / 27.0;
    return f >= minLogHz ? minLogMel + std::log(std::max(f, 1e-30) / minLogHz) / logstep : f / fsp;
}

double melToHz(double m) {
    constexpr double fsp = 200.0 / 3, minLogHz = 1000.0, minLogMel = minLogHz / fsp;
    const double logstep = std::log(6.4) / 27.0;
    return m >= minLogMel ? minLogHz * std::exp(logstep * (m - minLogMel)) : fsp * m;
}

// In-place radix-2 FFT, kFft points; `tw` holds e^(-2πik/kFft) for k < kFft/2.
void fft(std::complex<double>* a, const std::complex<double>* tw) {
    for (int i = 1, j = 0; i < kFft; ++i) {
        int bit = kFft >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= kFft; len <<= 1) {
        const int step = kFft / len;
        for (int i = 0; i < kFft; i += len)
            for (int k = 0; k < len / 2; ++k) {
                const auto u = a[i + k], v = a[i + k + len / 2] * tw[k * step];
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
    }
}

} // namespace

Mel::Mel() : filters_(std::size_t(kMels) * kBins), window_(kFft, 0.0), twiddles_(kFft / 2), spans_(kMels) {
    for (int k = 0; k < kFft / 2; ++k)
        twiddles_[k] = std::polar(1.0, -2 * std::numbers::pi * k / kFft);
    // librosa.filters.mel(sr=16000, n_fft=512, n_mels=128, fmin=0, fmax=8000, norm="slaney").
    std::vector<double> fftFreqs(kBins), melF(kMels + 2);
    for (int i = 0; i < kBins; ++i)
        fftFreqs[i] = double(kSampleRate) / 2 * i / (kBins - 1);
    const double lo = hzToMel(0.0), hi = hzToMel(kSampleRate / 2.0);
    for (int i = 0; i < kMels + 2; ++i)
        melF[i] = melToHz(lo + (hi - lo) * i / (kMels + 1));
    for (int m = 0; m < kMels; ++m) {
        const double enorm = 2.0 / (melF[m + 2] - melF[m]);
        for (int k = 0; k < kBins; ++k) {
            const double lower = -(melF[m] - fftFreqs[k]) / (melF[m + 1] - melF[m]);
            const double upper = (melF[m + 2] - fftFreqs[k]) / (melF[m + 2] - melF[m + 1]);
            filters_[std::size_t(m) * kBins + k] = float(std::max(0.0, std::min(lower, upper)) * enorm);
        }
    }
    // Each filter is non-zero over a few bins only.
    for (int m = 0; m < kMels; ++m) {
        int a = 0, b = 0;
        for (int k = 0; k < kBins; ++k)
            if (filters_[std::size_t(m) * kBins + k] != 0) {
                if (b == 0)
                    a = k;
                b = k + 1;
            }
        spans_[m] = {a, b};
    }
    // torch.hann_window(400, periodic=False), centred in the 512-point frame.
    const int left = (kFft - kWin) / 2;
    for (int n = 0; n < kWin; ++n)
        window_[left + n] = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * n / (kWin - 1));
}

std::vector<float> Mel::operator()(const float* pcm, std::size_t n) const {
    // Pre-emphasis, then the centre padding: n_fft / 2 zeros each side.
    std::vector<float> x(n + kFft, 0.0f);
    for (std::size_t i = 0; i < n; ++i)
        x[kFft / 2 + i] = i == 0 ? pcm[0] : pcm[i] - kPreemph * pcm[i - 1];
    const std::size_t frames = 1 + n / kHop;
    std::vector<float> out(frames * kMels);
    std::vector<std::complex<double>> buf(kFft);
    std::vector<double> power(kBins);
    for (std::size_t f = 0; f < frames; ++f) {
        const float* frame = x.data() + f * kHop;
        for (int i = 0; i < kFft; ++i)
            buf[i] = frame[i] * window_[i];
        fft(buf.data(), twiddles_.data());
        for (int k = 0; k < kBins; ++k)
            power[k] = std::norm(buf[k]);
        for (int m = 0; m < kMels; ++m) {
            const float* w = &filters_[std::size_t(m) * kBins];
            double e = 0;
            for (int k = spans_[m].first; k < spans_[m].second; ++k)
                e += w[k] * power[k];
            out[f * kMels + m] = float(std::log(e + kLogGuard));
        }
    }
    // Each band to zero mean, unit variance over the utterance (unbiased variance).
    for (int m = 0; m < kMels; ++m) {
        double mean = 0;
        for (std::size_t f = 0; f < frames; ++f)
            mean += out[f * kMels + m];
        mean /= double(frames);
        double var = 0;
        for (std::size_t f = 0; f < frames; ++f) {
            const double d = out[f * kMels + m] - mean;
            var += d * d;
        }
        var /= frames > 1 ? double(frames - 1) : 1.0;
        const double scale = 1.0 / (std::sqrt(var) + 1e-5);
        for (std::size_t f = 0; f < frames; ++f)
            out[f * kMels + m] = float((out[f * kMels + m] - mean) * scale);
    }
    return out;
}

} // namespace tala::phonon
