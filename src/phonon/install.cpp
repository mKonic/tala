#include "install.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <archive.h>
#include <archive_entry.h>
#include <curl/curl.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>

namespace tala::phonon {

namespace {

namespace fs = std::filesystem;

// Phonon-2 at the commit tala was measured with.
constexpr const char* kModelUrl =
    "https://huggingface.co/FermionResearch/Phonon-2/resolve/7e153e4054c0a10db6d47d6fff5c062d4c77c154/phonon-2.bps.tar.zst";
constexpr const char* kModelSha = "98125795b6dda72f5c6eee9ba33d19815df65dcb18b50a357bf9f73c9935309e";
constexpr const char* kLicenseUrl =
    "https://huggingface.co/FermionResearch/Phonon-2/resolve/7e153e4054c0a10db6d47d6fff5c062d4c77c154/LICENSE-WEIGHTS-CC-BY-4.0.txt";
constexpr const char* kNoticeUrl =
    "https://huggingface.co/FermionResearch/Phonon-2/resolve/7e153e4054c0a10db6d47d6fff5c062d4c77c154/NOTICE";
// fermion-research 0.2.11 (kernel ABI 1, TDT ABI 3).
constexpr const char* kWheelUrl =
    "https://files.pythonhosted.org/packages/35/c3/90dc124286ce01004ddb30cd302381aad79fc281899d7dd2973ec85faef6/"
    "fermion_research-0.2.11-py3-none-any.whl";
constexpr const char* kWheelSha = "c4097574ef42e96caf31732097321da9e16c0b9108b5c616aa9d5d1f540319f8";
constexpr const char* kKernelDir = "fermion/_speech/_engine_phonon2_cpu/";
#if defined(__aarch64__)
constexpr const char* kArch = "-linux-aarch64";
#else
constexpr const char* kArch = "-linux-x86_64";
#endif

struct Download {
    QByteArray data;
    std::function<void(long long, long long)> progress;
};

std::size_t onData(char* p, std::size_t size, std::size_t n, void* user) {
    static_cast<Download*>(user)->data.append(p, qsizetype(size * n));
    return size * n;
}

int onProgress(void* user, curl_off_t total, curl_off_t done, curl_off_t, curl_off_t) {
    auto* d = static_cast<Download*>(user);
    if (d->progress && total > 0)
        d->progress(done, total);
    return 0;
}

bool fetch(const std::string& url, Download& d, std::string& error) {
    CURL* c = curl_easy_init();
    if (!c) {
        error = "curl failed to start";
        return false;
    }
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "tala");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &d);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &d);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    const CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) {
        error = std::string("download failed: ") + curl_easy_strerror(rc) + " (" + url + ")";
        return false;
    }
    return true;
}

QByteArray sha256(const QByteArray& b) {
    return QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex();
}

bool writeFile(const fs::path& path, const QByteArray& data) {
    QFile f(QString::fromStdString(path.string()));
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

// Every member of an archive in memory, by name.
bool members(const QByteArray& blob, const std::function<bool(const std::string&, const QByteArray&)>& each,
             std::string& error) {
    archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (archive_read_open_memory(a, blob.constData(), std::size_t(blob.size())) != ARCHIVE_OK) {
        error = std::string("can't open the archive: ") + archive_error_string(a);
        archive_read_free(a);
        return false;
    }
    archive_entry* e;
    bool ok = true;
    while (ok && archive_read_next_header(a, &e) == ARCHIVE_OK) {
        if (archive_entry_filetype(e) != AE_IFREG)
            continue;
        QByteArray data(qsizetype(archive_entry_size(e)), Qt::Uninitialized);
        qsizetype at = 0;
        while (at < data.size()) {
            const la_ssize_t got = archive_read_data(a, data.data() + at, std::size_t(data.size() - at));
            if (got <= 0)
                break;
            at += got;
        }
        if (at != data.size()) {
            error = "the archive is damaged";
            ok = false;
            break;
        }
        ok = each(archive_entry_pathname(e), data);
    }
    archive_read_free(a);
    return ok;
}

// Fermion's byte-plane transport transform undone (package_release_bps.join_file).
QByteArray joinPlanes(const QByteArray& data, const QJsonObject& meta) {
    const qsizetype base = meta["base"].toInteger(), originalBytes = meta["original_bytes"].toInteger();
    const qsizetype payload = meta["payload_bytes"].toInteger();
    QByteArray out(originalBytes, Qt::Uninitialized);
    const char* raw = data.constData();
    char* o = out.data();
    std::memcpy(o, raw, std::size_t(base));
    qsizetype src = base, cursor = 0;
    for (const QJsonValue& v : meta["plan"].toArray()) {
        const QJsonArray span = v.toArray();
        const qsizetype start = span[0].toInteger(), end = span[1].toInteger(), item = span[2].toInteger();
        if (start > cursor) {
            std::memcpy(o + base + cursor, raw + src, std::size_t(start - cursor));
            src += start - cursor;
        }
        const qsizetype n = end - start, per = n / item;
        for (qsizetype i = 0; i < item; ++i)
            for (qsizetype j = 0; j < per; ++j)
                o[base + start + j * item + i] = raw[src + i * per + j];
        src += n;
        cursor = end;
    }
    if (payload > cursor)
        std::memcpy(o + base + cursor, raw + src, std::size_t(payload - cursor));
    return out;
}

bool installModel(const fs::path& dir, const std::function<void(const std::string&, long long, long long)>& progress,
                  std::string& error) {
    Download d;
    d.progress = [&](long long done, long long total) { progress("model", done, total); };
    if (!fetch(kModelUrl, d, error))
        return false;
    if (sha256(d.data) != kModelSha) {
        error = "the model download doesn't match its checksum";
        return false;
    }
    const fs::path tmp = dir.string() + ".new";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    std::map<std::string, QJsonObject> index;
    int written = 0;
    const bool ok = members(d.data, [&](const std::string& name, const QByteArray& blob) {
        if (name == "bps_manifest.json") {
            for (const QJsonValue& row : QJsonDocument::fromJson(blob).object()["files"].toArray())
                index[row["path"].toString().toStdString()] = row.toObject();
            return true;
        }
        if (index.empty()) {
            error = "bps_manifest.json must come first in the model archive";
            return false;
        }
        const std::string rel = name.ends_with(".bps") ? name.substr(0, name.size() - 4) : name;
        auto it = index.find(rel);
        if (it == index.end() || rel.find("..") != std::string::npos) {
            error = "unexpected file in the model archive: " + name;
            return false;
        }
        const QJsonObject& row = it->second;
        const QByteArray data = row.contains("transform") ? joinPlanes(blob, row["transform"].toObject()) : blob;
        if (sha256(data) != row["original_sha256"].toString().toLatin1() ||
            data.size() != row["original_bytes"].toInteger()) {
            error = "checksum mismatch on " + rel;
            return false;
        }
        fs::create_directories((tmp / rel).parent_path());
        if (!writeFile(tmp / rel, data)) {
            error = "can't write " + (tmp / rel).string();
            return false;
        }
        ++written;
        return true;
    }, error);
    if (!ok || written != int(index.size())) {
        if (error.empty())
            error = "the model archive is missing files";
        fs::remove_all(tmp);
        return false;
    }
    // The licence and notice go with the weights (CC-BY-4.0 asks for attribution).
    for (const char* url : {kLicenseUrl, kNoticeUrl}) {
        Download l;
        if (fetch(url, l, error))
            writeFile(tmp / fs::path(url).filename(), l.data);
    }
    error.clear();
    fs::remove_all(dir);
    fs::rename(tmp, dir);
    return true;
}

bool installKernels(const fs::path& dir, const std::function<void(const std::string&, long long, long long)>& progress,
                    std::string& error) {
    Download d;
    d.progress = [&](long long done, long long total) { progress("kernels", done, total); };
    if (!fetch(kWheelUrl, d, error))
        return false;
    if (sha256(d.data) != kWheelSha) {
        error = "the kernel download doesn't match its checksum";
        return false;
    }
    std::map<std::string, QByteArray> files;
    std::map<std::string, std::string> pins;
    const bool ok = members(d.data, [&](const std::string& name, const QByteArray& blob) {
        if (name.starts_with(kKernelDir)) {
            const std::string base = name.substr(std::string(kKernelDir).size());
            if (base == "SHA256SUMS.txt") {
                for (const QByteArray& line : blob.split('\n')) {
                    const QList<QByteArray> parts = line.simplified().split(' ');
                    if (parts.size() == 2)
                        pins[parts[1].toStdString()] = parts[0].toStdString();
                }
            } else if (base.starts_with("libphonon2_") && base.find(kArch) != std::string::npos) {
                files[base] = blob;
            }
        } else if (name.ends_with(".dist-info/licenses/LICENSE") || name.ends_with(".dist-info/licenses/NOTICE")) {
            files[fs::path(name).filename().string()] = blob;
        }
        return true;
    }, error);
    if (!ok)
        return false;
    const fs::path tmp = dir.string() + ".new";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    for (auto& [name, blob] : files) {
        if (name.starts_with("libphonon2_") && (!pins.contains(name) || sha256(blob).toStdString() != pins[name])) {
            error = name + " doesn't match the package's own checksum";
            fs::remove_all(tmp);
            return false;
        }
        if (!writeFile(tmp / name, blob)) {
            error = "can't write " + (tmp / name).string();
            fs::remove_all(tmp);
            return false;
        }
    }
    fs::remove_all(dir);
    fs::rename(tmp, dir);
    return true;
}

} // namespace

bool installed(const std::string& dir) {
    const fs::path d(dir);
    return fs::exists(d / "model/model.fermion") && fs::exists(d / "model/config.json") &&
           fs::exists(d / "kernels/libphonon2_tdt-linux-x86_64.so");
}

bool install(const std::string& dir, const std::function<void(const std::string&, long long, long long)>& progress,
             std::string& error) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    fs::create_directories(dir);
    const bool ok = (fs::exists(fs::path(dir) / "kernels/libphonon2_tdt-linux-x86_64.so") ||
                     installKernels(fs::path(dir) / "kernels", progress, error)) &&
                    (fs::exists(fs::path(dir) / "model/model.fermion") ||
                     installModel(fs::path(dir) / "model", progress, error));
    curl_global_cleanup();
    return ok;
}

} // namespace tala::phonon
