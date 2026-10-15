#include "phonon.hpp"

#include "hotwords.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <malloc.h>
#include <map>
#include <set>
#include <thread>
#include <unistd.h>

namespace tala::phonon {

namespace {

// The base architecture (nvidia/parakeet-tdt-0.6b-v3), as Fermion's engine fixes it.
constexpr int kLayers = 24, kHidden = 1024, kFfn = 4096, kHeads = 8, kConvKernel = 9, kSubChannels = 256;
constexpr int kBlank = 8192, kVocab = 8193, kMaxSymbols = 10;
constexpr int kDurations[] = {0, 1, 2, 3, 4};
constexpr const char* kFormat = "fermion-five-value-parakeet-v1";

// Long audio: windows of 25-35 s cut in the longest pause (Fermion's segment.py).
constexpr double kSingleShotS = 35.0, kBandMinS = 25.0, kBandMaxS = 35.0, kTargetS = 30.0, kBlockS = 0.05;
constexpr double kNoiseFloor = 0.004, kGateRatio = 0.18;

// The order phonon2_enc_set_layer takes a layer's matrices and vectors in.
constexpr const char* kMats[] = {
    "feed_forward1.linear1", "feed_forward1.linear2", "self_attn.q_proj", "self_attn.k_proj",
    "self_attn.v_proj", "self_attn.o_proj", "self_attn.relative_k_proj", "conv.pointwise_conv1",
    "conv.pointwise_conv2", "feed_forward2.linear1", "feed_forward2.linear2",
};
constexpr const char* kVecs[] = {
    "norm_feed_forward1.weight", "norm_feed_forward1.bias", "norm_self_att.weight", "norm_self_att.bias",
    "norm_conv.weight", "norm_conv.bias", "norm_feed_forward2.weight", "norm_feed_forward2.bias",
    "norm_out.weight", "norm_out.bias", "self_attn.bias_u", "self_attn.bias_v",
    "conv.depthwise_conv.weight", "conv.norm.weight", "conv.norm.bias", "conv.norm.running_mean",
    "conv.norm.running_var",
};

float half(std::uint16_t h) {
    const std::uint32_t sign = std::uint32_t(h & 0x8000) << 16;
    std::uint32_t exp = (h >> 10) & 0x1f, mant = h & 0x3ff;
    std::uint32_t bits;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {  // subnormal: normalise
            int e = -1;
            do {
                ++e;
                mant <<= 1;
            } while (!(mant & 0x400));
            bits = sign | std::uint32_t(127 - 15 - e) << 23 | (mant & 0x3ff) << 13;
        }
    } else if (exp == 31) {
        bits = sign | 0x7f800000 | mant << 13;
    } else {
        bits = sign | (exp + 127 - 15) << 23 | mant << 13;
    }
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// An intN record: int8 values [rows][cols] and one fp16 scale per row.
struct IntRecord {
    std::vector<std::int8_t> q;
    std::vector<std::uint16_t> scale;
    int rows = 0, cols = 0;
};

IntRecord readInt(const std::string& blob, const std::vector<long long>& shape, int bits) {
    IntRecord r;
    r.rows = int(shape[0]);
    long long total = 1;
    for (long long s : shape)
        total *= s;
    r.cols = int(total / r.rows);
    const std::size_t scaleBytes = 2 * std::size_t(r.rows);
    r.scale.resize(std::size_t(r.rows));
    std::memcpy(r.scale.data(), blob.data() + blob.size() - scaleBytes, scaleBytes);
    const auto* body = reinterpret_cast<const std::uint8_t*>(blob.data());
    r.q.resize(std::size_t(total));
    if (bits == 8) {
        std::memcpy(r.q.data(), body, std::size_t(total));
    } else {  // 6-bit, four values in three bytes, little-endian
        for (long long i = 0; i < total; ++i) {
            const std::size_t g = std::size_t(i / 4) * 3;
            const std::uint32_t packed = body[g] | std::uint32_t(body[g + 1]) << 8 | std::uint32_t(body[g + 2]) << 16;
            r.q[std::size_t(i)] = std::int8_t(int((packed >> (6 * (i % 4))) & 0x3f) - 32);
        }
    }
    return r;
}

std::vector<float> dense(const IntRecord& r) {
    std::vector<float> out(r.q.size());
    for (int o = 0; o < r.rows; ++o) {
        const float s = half(r.scale[std::size_t(o)]);
        for (int c = 0; c < r.cols; ++c)
            out[std::size_t(o) * r.cols + c] = float(r.q[std::size_t(o) * r.cols + c]) * s;
    }
    return out;
}

std::vector<float> fromHalf(const std::string& blob) {
    std::vector<float> out(blob.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        std::uint16_t h;
        std::memcpy(&h, blob.data() + 2 * i, 2);
        out[i] = half(h);
    }
    return out;
}

bool special(const std::string& p) {
    return (p.starts_with("<|") && p.ends_with("|>")) || p == "<unk>" || p == "<pad>";
}

bool cpuHasAvx2() {
#if defined(__x86_64__)
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2");
#else
    return true;
#endif
}

} // namespace

int defaultThreads() {
    const unsigned logical = std::max(1u, std::thread::hardware_concurrency());
    // Physical cores: distinct (package, core id) pairs.
    std::set<std::pair<std::string, std::string>> cores;
    for (unsigned c = 0; c < logical; ++c) {
        auto read = [](const std::string& path) {
            std::ifstream f(path);
            std::string s;
            std::getline(f, s);
            return s;
        };
        const std::string base = "/sys/devices/system/cpu/cpu" + std::to_string(c) + "/topology/";
        const std::string core = read(base + "core_id"), pkg = read(base + "physical_package_id");
        if (core.empty())
            return int(std::min(logical, 16u));
        cores.insert({pkg, core});
    }
    // SMT siblings slow the kernels down on larger parts.
    const unsigned phys = unsigned(cores.size());
    return int(std::min(phys >= 6 ? phys : logical, 16u));
}

struct Model::Impl {
    void* enc = nullptr;  // libphonon2_enc
    void* tdt = nullptr;  // libphonon2_tdt

    // libphonon2_enc
    void (*cpuSetThreads)(int) = nullptr;
    int (*createMany)(int, const int*, const int*, const void* const*, const std::size_t*, void**) = nullptr;
    void (*cpuDestroy)(void*) = nullptr;
    void* (*encCreate)(int, int, int, int, int, int, int) = nullptr;
    int (*encSetSubsampling)(void*, const float*, const float*, const float*, const float*, const float*, const float*,
                             const float*, const float*, const float*, const float*, const float*, const float*,
                             const float*) = nullptr;
    int (*encSetLayer)(void*, int, void* const*, const float* const*) = nullptr;
    int (*encOutLen)(void*, int) = nullptr;
    int (*encForward)(void*, const float*, int, float*) = nullptr;
    void (*encDestroy)(void*) = nullptr;
    // libphonon2_tdt
    void* (*tdtCreate)(int, int, int, int, int, const int*, int, int, const void*, const void*, const void*, const void*,
                       const void*, const void*, const void*, const void*, const void*, const void*, const void*,
                       const void*, const void*, const void*, const void*, const void*, const void*, const void*,
                       const void*, const void*) = nullptr;
    int (*tdtDecodeTimed)(void*, const float*, int, int*, int*, int*, int) = nullptr;
    int (*tdtSetBias)(void*, int, int, const std::int32_t*, const std::int32_t*, const std::int32_t*, int,
                      const std::int32_t*, float) = nullptr;
    void (*tdtSetThreads)(int) = nullptr;
    void (*tdtDestroy)(void*) = nullptr;

    void* encoder = nullptr;
    void* decoder = nullptr;
    std::vector<void*> matrices;  // the encoder's packed weights (it uses, doesn't own, them)
    std::vector<float> projW, projB;  // encoder_projector [640][1024], [640]
    int projOut = 0;
    std::vector<std::string> vocab;
    // The decoder's tables, kept alive as long as it is.
    std::vector<IntRecord> tdtInts;
    std::vector<std::vector<std::uint16_t>> tdtHalves;

    ~Impl() {
        if (decoder && tdtDestroy)
            tdtDestroy(decoder);
        if (encoder && encDestroy)
            encDestroy(encoder);
        for (void* m : matrices)
            if (m && cpuDestroy)
                cpuDestroy(m);
        // The libraries stay loaded: their thread pools may still be winding down.
    }

    template <typename F>
    bool sym(void* lib, F& fn, const char* name, std::string& error) {
        fn = reinterpret_cast<F>(dlsym(lib, name));
        if (!fn)
            error = std::string("missing ") + name;
        return fn != nullptr;
    }
};

Model::Model() = default;
Model::~Model() = default;

bool Model::load(const std::string& modelDir, const std::string& libDir, int threads, std::string& error) {
    d_ = std::make_unique<Impl>();
    Impl& m = *d_;
    threads_ = threads > 0 ? threads : defaultThreads();

    // The encoder library carries the packed GEMM too; the -base build is for
    // processors without AVX2.
    libraryName_ = cpuHasAvx2() ? "libphonon2_enc-linux-x86_64.so" : "libphonon2_enc-linux-x86_64-base.so";
    m.enc = dlopen((libDir + "/" + libraryName_).c_str(), RTLD_NOW | RTLD_LOCAL);
    m.tdt = dlopen((libDir + "/libphonon2_tdt-linux-x86_64.so").c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m.enc || !m.tdt) {
        error = std::string("can't load the Phonon-2 kernels: ") + dlerror();
        return false;
    }
    int (*cpuAbi)() = nullptr, (*encAbi)() = nullptr, (*tdtAbi)() = nullptr;
    unsigned (*features)() = nullptr, (*buildFeatures)() = nullptr;
    if (!m.sym(m.enc, cpuAbi, "phonon2_cpu_abi_version", error) || !m.sym(m.enc, encAbi, "phonon2_enc_abi_version", error) ||
        !m.sym(m.tdt, tdtAbi, "phonon2_tdt_abi_version", error) || !m.sym(m.enc, features, "phonon2_cpu_features", error) ||
        !m.sym(m.enc, buildFeatures, "phonon2_cpu_build_features", error) ||
        !m.sym(m.enc, m.cpuSetThreads, "phonon2_cpu_set_threads", error) ||
        !m.sym(m.enc, m.createMany, "phonon2_cpu_create_onedot_wire_many", error) ||
        !m.sym(m.enc, m.cpuDestroy, "phonon2_cpu_destroy", error) || !m.sym(m.enc, m.encCreate, "phonon2_enc_create", error) ||
        !m.sym(m.enc, m.encSetSubsampling, "phonon2_enc_set_subsampling", error) ||
        !m.sym(m.enc, m.encSetLayer, "phonon2_enc_set_layer", error) ||
        !m.sym(m.enc, m.encOutLen, "phonon2_enc_out_len", error) ||
        !m.sym(m.enc, m.encForward, "phonon2_enc_forward", error) ||
        !m.sym(m.enc, m.encDestroy, "phonon2_enc_destroy", error) ||
        !m.sym(m.tdt, m.tdtCreate, "phonon2_tdt_create", error) ||
        !m.sym(m.tdt, m.tdtDecodeTimed, "phonon2_tdt_decode_timed", error) ||
        !m.sym(m.tdt, m.tdtSetBias, "phonon2_tdt_set_bias", error) ||
        !m.sym(m.tdt, m.tdtSetThreads, "phonon2_tdt_set_threads", error) ||
        !m.sym(m.tdt, m.tdtDestroy, "phonon2_tdt_destroy", error))
        return false;
    if (cpuAbi() != 1 || encAbi() != 1) {
        error = "the Phonon-2 kernels have an ABI tala doesn't know";
        return false;
    }
    if (buildFeatures() & ~features()) {
        error = libraryName_ + " needs CPU features this processor doesn't have";
        return false;
    }
    m.cpuSetThreads(threads_);
    m.tdtSetThreads(threads_);

    // The vocabulary (config.json's joint.vocabulary).
    {
        QFile f(QString::fromStdString(modelDir + "/config.json"));
        if (!f.open(QIODevice::ReadOnly)) {
            error = "no config.json in " + modelDir;
            return false;
        }
        const QJsonObject c = QJsonDocument::fromJson(f.readAll()).object();
        QJsonArray v = c.value("vocabulary").toArray();
        if (v.isEmpty())
            v = c.value("joint").toObject().value("vocabulary").toArray();
        for (const QJsonValue& p : v)
            m.vocab.push_back(p.toString().toStdString());
        if (m.vocab.empty()) {
            error = "config.json has no vocabulary";
            return false;
        }
    }

    // One pass over the container.
    std::ifstream in(modelDir + "/model.fermion", std::ios::binary);
    std::uint64_t headerLen = 0;
    if (!in.read(reinterpret_cast<char*>(&headerLen), 8) || headerLen > (64u << 20)) {
        error = "can't read model.fermion";
        return false;
    }
    std::string headerText(headerLen, '\0');
    in.read(headerText.data(), std::streamsize(headerLen));
    const QJsonObject header = QJsonDocument::fromJson(QByteArray::fromStdString(headerText)).object();
    if (header.value("format").toString() != kFormat) {
        error = "model.fermion is not a " + std::string(kFormat) + " container";
        return false;
    }

    std::map<std::string, std::vector<float>> floats;   // everything the encoder takes as fp32
    std::map<std::string, IntRecord> ints;              // decoder and joint, as stored
    std::map<std::string, std::string> halves;          // decoder and joint biases, fp16 as stored
    std::map<std::string, void*> packed;
    struct Pending {
        std::string name;
        int rows, cols;
        std::string blob;
    };
    std::vector<Pending> pending;
    auto flush = [&] {
        if (pending.empty())
            return;
        const int n = int(pending.size());
        std::vector<int> rows(n), cols(n);
        std::vector<const void*> ptrs(n);
        std::vector<std::size_t> lens(n);
        std::vector<void*> out(n, nullptr);
        for (int i = 0; i < n; ++i) {
            rows[i] = pending[i].rows;
            cols[i] = pending[i].cols;
            ptrs[i] = pending[i].blob.data();
            lens[i] = pending[i].blob.size();
        }
        m.createMany(n, rows.data(), cols.data(), ptrs.data(), lens.data(), out.data());
        for (int i = 0; i < n; ++i) {
            packed[pending[i].name] = out[i];
            m.matrices.push_back(out[i]);
        }
        pending.clear();
    };
    bool bad = false;
    for (const QJsonValue& v : header.value("index").toArray()) {
        const QJsonObject e = v.toObject();
        const std::string name = e.value("n").toString().toStdString(), kind = e.value("k").toString().toStdString();
        std::vector<long long> shape;
        for (const QJsonValue& s : e.value("shape").toArray())
            shape.push_back(s.toInteger());
        std::string blob(std::size_t(e.value("b").toInteger()), '\0');
        if (!in.read(blob.data(), std::streamsize(blob.size()))) {
            bad = true;
            break;
        }
        const bool tdtPart = name.starts_with("decoder.") || name.starts_with("joint.");
        if (kind == "five_value") {
            pending.push_back({name, int(shape.at(0)), int(shape.at(1)), std::move(blob)});
            if (pending.size() >= 16)
                flush();
        } else if (kind == "int6" || kind == "int8") {
            IntRecord r = readInt(blob, shape, kind == "int8" ? 8 : 6);
            if (tdtPart)
                ints[name] = std::move(r);
            else
                floats[name] = dense(r);
        } else if (kind == "fp16") {
            if (tdtPart)
                halves[name] = std::move(blob);
            else
                floats[name] = fromHalf(blob);
        } else {
            error = "unknown record kind " + kind;
            return false;
        }
    }
    flush();
    if (bad) {
        error = "model.fermion is cut short";
        return false;
    }

    // The encoder.
    m.encoder = m.encCreate(kLayers, kHidden, kFfn, kHeads, kConvKernel, kMels, kSubChannels);
    if (!m.encoder) {
        error = "phonon2_enc_create failed";
        return false;
    }
    std::vector<float> invFreq(kHidden / 2);
    for (int i = 0; i < kHidden / 2; ++i)
        invFreq[std::size_t(i)] = 1.0f / std::pow(10000.0f, float(2 * i) / float(kHidden));
    auto F = [&](const std::string& n) -> const float* {
        auto it = floats.find(n);
        if (it == floats.end()) {
            error = "model.fermion lacks " + n;
            return nullptr;
        }
        return it->second.data();
    };
    const std::string S = "encoder.subsampling.";
    const float* sub[12] = {F(S + "layers.0.weight"), F(S + "layers.0.bias"), F(S + "layers.2.weight"), F(S + "layers.2.bias"),
                            F(S + "layers.3.weight"), F(S + "layers.3.bias"), F(S + "layers.5.weight"), F(S + "layers.5.bias"),
                            F(S + "layers.6.weight"), F(S + "layers.6.bias"), F(S + "linear.weight"), F(S + "linear.bias")};
    if (!error.empty())
        return false;
    if (m.encSetSubsampling(m.encoder, sub[0], sub[1], sub[2], sub[3], sub[4], sub[5], sub[6], sub[7], sub[8], sub[9],
                            sub[10], sub[11], invFreq.data()) != 0) {
        error = "phonon2_enc_set_subsampling failed";
        return false;
    }
    for (int l = 0; l < kLayers; ++l) {
        const std::string P = "encoder.layers." + std::to_string(l) + ".";
        void* mats[11];
        const float* vecs[17];
        for (int i = 0; i < 11; ++i) {
            auto it = packed.find(P + kMats[i]);
            if (it == packed.end() || !it->second) {
                error = "model.fermion lacks a packed " + P + kMats[i];
                return false;
            }
            mats[i] = it->second;
        }
        for (int i = 0; i < 17; ++i)
            if (!(vecs[i] = F(P + kVecs[i])))
                return false;
        if (m.encSetLayer(m.encoder, l, mats, vecs) != 0) {
            error = "phonon2_enc_set_layer failed at layer " + std::to_string(l);
            return false;
        }
    }

    // The projector from the encoder's width to the joint's.
    if (!F("encoder_projector.weight") || !F("encoder_projector.bias"))
        return false;
    m.projW = std::move(floats["encoder_projector.weight"]);
    m.projB = std::move(floats["encoder_projector.bias"]);
    m.projOut = int(m.projB.size());
    floats.clear();  // the encoder copied every vector

    // The decoder: prediction network and joint head, int6 with fp16 scales and biases.
    const char* intNames[] = {"decoder.embedding.weight", "decoder.lstm.weight_ih_l0", "decoder.lstm.weight_hh_l0",
                              "decoder.lstm.weight_ih_l1", "decoder.lstm.weight_hh_l1",
                              "decoder.decoder_projector.weight", "joint.head.weight"};
    const char* halfNames[] = {"decoder.lstm.bias_ih_l0", "decoder.lstm.bias_hh_l0", "decoder.lstm.bias_ih_l1",
                               "decoder.lstm.bias_hh_l1", "decoder.decoder_projector.bias", "joint.head.bias"};
    for (const char* n : intNames) {
        if (!ints.contains(n)) {
            error = std::string("model.fermion lacks ") + n;
            return false;
        }
        m.tdtInts.push_back(std::move(ints[n]));
    }
    for (const char* n : halfNames) {
        if (!halves.contains(n)) {
            error = std::string("model.fermion lacks ") + n;
            return false;
        }
        const std::string& b = halves[n];
        std::vector<std::uint16_t> h(b.size() / 2);
        std::memcpy(h.data(), b.data(), b.size());
        m.tdtHalves.push_back(std::move(h));
    }
    const IntRecord &emb = m.tdtInts[0], &ih0 = m.tdtInts[1], &hh0 = m.tdtInts[2], &ih1 = m.tdtInts[3],
                    &hh1 = m.tdtInts[4], &pr = m.tdtInts[5], &head = m.tdtInts[6];
    const auto& hb = m.tdtHalves;
    m.decoder = m.tdtCreate(emb.rows, emb.cols, hh0.cols, head.rows, int(std::size(kDurations)), kDurations, kBlank,
                            kMaxSymbols, emb.q.data(), emb.scale.data(), ih0.q.data(), ih0.scale.data(), hb[0].data(),
                            hh0.q.data(), hh0.scale.data(), hb[1].data(), ih1.q.data(), ih1.scale.data(), hb[2].data(),
                            hh1.q.data(), hh1.scale.data(), hb[3].data(), pr.q.data(), pr.scale.data(), hb[4].data(),
                            head.q.data(), head.scale.data(), hb[5].data());
    if (!m.decoder) {
        error = "phonon2_tdt_create failed";
        return false;
    }
    // The container's bytes are gone; give the pages back (glibc keeps them otherwise).
    malloc_trim(0);
    if (emb.rows != kVocab)
        std::fprintf(stderr, "tala-engine: vocabulary rows %d, expected %d\n", emb.rows, kVocab);
    return true;
}

std::string Model::decodeWindow(const float* pcm, std::size_t n, const Hotwords* bias) {
    Impl& m = *d_;
    using Clock = std::chrono::steady_clock;
    const bool profile = std::getenv("TALA_PROFILE");
    auto mark = Clock::now();
    auto lap = [&](const char* what) {
        if (!profile)
            return;
        const auto now = Clock::now();
        std::fprintf(stderr, "  %-9s %6.1f ms\n", what, std::chrono::duration<double, std::milli>(now - mark).count());
        mark = now;
    };
    const std::vector<float> feats = mel_(pcm, n);
    lap("mel");
    const int T = int(feats.size() / kMels);
    // Debugging against Fermion's own front end: TALA_DUMP=prefix writes the features and the projector output.
    const char* dump = std::getenv("TALA_DUMP");
    if (dump) {
        std::ofstream(std::string(dump) + ".mel", std::ios::binary).write(reinterpret_cast<const char*>(feats.data()), std::streamsize(feats.size() * 4));
    }
    const int T8 = m.encOutLen(m.encoder, T);
    std::vector<float> enc(std::size_t(T8) * kHidden);
    if (m.encForward(m.encoder, feats.data(), T, enc.data()) != 0)
        return {};
    lap("encoder");
    // encp = enc · Wᵀ + b
    const int O = m.projOut;
    std::vector<float> encp(std::size_t(T8) * O);
    for (int t = 0; t < T8; ++t) {
        const float* x = &enc[std::size_t(t) * kHidden];
        for (int o = 0; o < O; ++o) {
            const float* w = &m.projW[std::size_t(o) * kHidden];
            float acc[8] = {};
            for (int k = 0; k < kHidden; k += 8)
                for (int j = 0; j < 8; ++j)
                    acc[j] += x[k + j] * w[k + j];
            encp[std::size_t(t) * O + o] = acc[0] + acc[1] + acc[2] + acc[3] + acc[4] + acc[5] + acc[6] + acc[7] + m.projB[std::size_t(o)];
        }
    }
    lap("projector");
    if (dump)
        std::ofstream(std::string(dump) + ".encp", std::ios::binary).write(reinterpret_cast<const char*>(encp.data()), std::streamsize(encp.size() * 4));
    const int cap = std::max(64, 12 * T8);
    const auto room = static_cast<std::size_t>(cap);
    std::vector<int> ids(room), frames(room), durs(room);
    if (bias && !bias->empty())
        m.tdtSetBias(m.decoder, bias->states(), bias->columns(), bias->table(), bias->toks(), bias->col(),
                     bias->colSize(), bias->depth(), bias->lambda());
    const int k = m.tdtDecodeTimed(m.decoder, encp.data(), T8, ids.data(), frames.data(), durs.data(), cap);
    lap("decoder");
    if (bias && !bias->empty())  // the tables aren't copied: disarm before they go
        m.tdtSetBias(m.decoder, 0, 0, nullptr, nullptr, nullptr, 0, nullptr, 0.0f);
    std::string text;
    for (int i = 0; i < k; ++i) {
        const int id = ids[std::size_t(i)];
        if (id < 0 || std::size_t(id) >= m.vocab.size() || special(m.vocab[std::size_t(id)]))
            continue;
        text += m.vocab[std::size_t(id)];
    }
    // "▁" marks a word start.
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        if (text.compare(i, 3, "▁") == 0) {
            out += ' ';
            i += 3;
        } else {
            out += text[i++];
        }
    }
    const auto first = out.find_first_not_of(' ');
    const auto last = out.find_last_not_of(' ');
    return first == std::string::npos ? std::string() : out.substr(first, last - first + 1);
}

Result Model::transcribe(const float* pcm, std::size_t n, const std::vector<std::string>& hotwords) {
    using Clock = std::chrono::steady_clock;
    Result r;
    r.audioSeconds = double(n) / kSampleRate;
    auto silent = [](const float* p, std::size_t len) {
        float peak = 0;
        for (std::size_t i = 0; i < len; ++i)
            peak = std::max(peak, std::fabs(p[i]));
        return len == 0 || peak < 1e-4f;
    };
    if (!d_ || !d_->encoder || silent(pcm, n))
        return r;
    const auto started = Clock::now();
    std::unique_ptr<Hotwords> bias;
    if (!hotwords.empty())
        bias = std::make_unique<Hotwords>(hotwords, d_->vocab);

    // Windows: one up to 35 s, else 25-35 s each, cut in the longest pause.
    std::vector<std::pair<std::size_t, std::size_t>> windows;
    const auto single = std::size_t(kSingleShotS * kSampleRate);
    if (n <= single) {
        windows.push_back({0, n});
    } else {
        const auto block = std::size_t(kBlockS * kSampleRate);
        const std::size_t blocks = (n + block - 1) / block;
        std::vector<double> rms(blocks);
        for (std::size_t b = 0; b < blocks; ++b) {
            double s = 0;
            for (std::size_t i = b * block; i < (b + 1) * block; ++i) {
                const double v = i < n ? pcm[i] : 0.0;
                s += v * v;
            }
            rms[b] = std::sqrt(s / double(block));
        }
        std::size_t start = 0;
        while (n - start > single) {
            const std::size_t lo = start + std::size_t(kBandMinS * kSampleRate), hi = start + std::size_t(kBandMaxS * kSampleRate);
            const std::size_t b0 = (lo + block - 1) / block, b1 = hi / block;
            double peak = 0;
            for (std::size_t b = start / block; b < b1; ++b)
                peak = std::max(peak, rms[b]);
            const double gate = std::max(kNoiseFloor, kGateRatio * peak);
            const std::size_t target = start + std::size_t(kTargetS * kSampleRate);
            std::size_t cut = target;
            // The longest quiet run, nearest 30 s on a tie; cut in its middle.
            long long bestLen = -1, bestDist = 0;
            for (std::size_t i = b0; i < b1;) {
                if (rms[i] > gate) {
                    ++i;
                    continue;
                }
                std::size_t j = i;
                while (j < b1 && rms[j] <= gate)
                    ++j;
                const std::size_t mid = (i + j) * block / 2;
                const long long len = (long long)(j - i), dist = -std::llabs((long long)mid - (long long)target);
                if (len > bestLen || (len == bestLen && (dist > bestDist || (dist == bestDist && mid > cut)))) {
                    bestLen = len;
                    bestDist = dist;
                    cut = mid;
                }
                i = j;
            }
            cut = std::max(lo, std::min({hi, cut, n}));
            windows.push_back({start, cut});
            start = cut;
        }
        windows.push_back({start, n});
    }
    for (auto [a, b] : windows) {
        if (silent(pcm + a, b - a))
            continue;
        const std::string t = decodeWindow(pcm + a, b - a, bias.get());
        if (!t.empty())
            r.text += (r.text.empty() ? "" : " ") + t;
    }
    r.decodeSeconds = std::chrono::duration<double>(Clock::now() - started).count();
    return r;
}

} // namespace tala::phonon
