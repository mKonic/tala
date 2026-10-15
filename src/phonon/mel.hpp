#pragma once
// Phonon-2's front end: 128 log-mel bands every 10 ms, the way Fermion's CPU
// engine computes them (the HF ParakeetFeatureExtractor's arithmetic):
// pre-emphasis 0.97, 512-point STFT with hop 160 and a 400-sample symmetric
// Hann window (centred, zero padded), Slaney mel filters, log(x + 2^-24), then
// each band normalised over the utterance.

#include <complex>
#include <cstddef>
#include <utility>
#include <vector>

namespace tala::phonon {

constexpr int kSampleRate = 16000;
constexpr int kMels = 128;

class Mel {
public:
    Mel();
    // [frames][kMels], row-major. Frames = samples / 160 + 1.
    std::vector<float> operator()(const float* pcm, std::size_t n) const;

private:
    std::vector<float> filters_;  // [kMels][257]
    std::vector<double> window_;  // 512, the 400-sample Hann centred in it
    std::vector<std::complex<double>> twiddles_;
    std::vector<std::pair<int, int>> spans_;  // each filter's non-zero bins
};

} // namespace tala::phonon
