#pragma once
// Phonon-2 on the CPU without Python: Fermion Research's own native kernels
// (libphonon2_enc for the FastConformer encoder, libphonon2_tdt for the greedy
// TDT decoder; Apache-2.0, from the fermion-research package) driven straight
// from the model container, the way their CPU engine's C path does it. tala's
// part is what their Python did around those calls: reading the container,
// the log-mel front end, the encoder projector, long-audio windows,
// hotwords and the vocabulary.

#include "mel.hpp"

#include <memory>
#include <string>
#include <vector>

namespace tala::phonon {

struct Result {
    std::string text;
    double decodeSeconds = 0;
    double audioSeconds = 0;
};

class Model {
public:
    Model();
    ~Model();
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    // `modelDir` holds model.fermion and config.json; `libDir` the
    // libphonon2_* shared objects. 0 threads: one per physical core.
    bool load(const std::string& modelDir, const std::string& libDir, int threads, std::string& error);

    // 16 kHz mono in [-1, 1].
    Result transcribe(const float* pcm, std::size_t n, const std::vector<std::string>& hotwords);

    int threads() const { return threads_; }
    std::string library() const { return libraryName_; }

private:
    struct Impl;
    std::string decodeWindow(const float* pcm, std::size_t n, const class Hotwords* bias);

    std::unique_ptr<Impl> d_;
    Mel mel_;
    int threads_ = 0;
    std::string libraryName_;
};

// One per physical core (Fermion's rule for x86 from six cores up and AMD
// parts), at most 16.
int defaultThreads();

} // namespace tala::phonon
