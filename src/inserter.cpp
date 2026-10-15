#include "inserter.hpp"

#include "ext-data-control-v1-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QTimer>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

namespace tala {

namespace {

Q_LOGGING_CATEGORY(lc, "tala.inserter")

constexpr const char* kTextMimes[] = {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "STRING", "TEXT"};
// X11 selection bookkeeping, not content.
constexpr std::string_view kMetaMimes[] = {"TARGETS", "MULTIPLE", "TIMESTAMP", "SAVE_TARGETS", "DELETE"};
constexpr std::size_t kMaxSaved = 64 << 20;
// How long the app gets to read the text, and to read the old clipboard back.
constexpr int kReadGraceMs = 500;
constexpr int kNoReadMs = 2500;

std::uint32_t now() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return std::uint32_t(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

Inserter::Saved textContent(const QString& text) {
    Inserter::Saved s;
    const std::string utf8 = text.toStdString();
    for (const char* m : kTextMimes)
        s.data.emplace_back(m, utf8);
    return s;
}

} // namespace

// A pipe being written to an app that pastes.
struct Inserter::Source : QObject {
    int fd;
    std::string data;
    std::size_t done = 0;
    QSocketNotifier notifier;
    Source(int fd, std::string data) : fd(fd), data(std::move(data)), notifier(fd, QSocketNotifier::Write) {
        connect(&notifier, &QSocketNotifier::activated, this, [this] { pump(); });
    }
    ~Source() override { close(fd); }
    void pump() {
        while (done < data.size()) {
            const ssize_t n = ::write(fd, data.data() + done, data.size() - done);
            if (n > 0) {
                done += std::size_t(n);
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EINTR))
                return;
            break;
        }
        notifier.setEnabled(false);
        deleteLater();
    }
};

// The clipboard and primary selection read out before tala replaces them.
struct Inserter::Snapshot : QObject {
    struct Pipe {
        int fd;
        bool primary;
        std::string mime;
        std::string data;
        std::unique_ptr<QSocketNotifier> notifier;
    };
    std::vector<std::unique_ptr<Pipe>> pipes;
    Saved clipboard, primary;
    std::size_t total = 0;
    std::function<void(Saved, Saved)> done;
    QTimer deadline;
    bool finished = false;

    void finish() {
        if (std::exchange(finished, true))
            return;
        for (auto& p : pipes) {
            if (p->fd >= 0)
                close(p->fd);
            (p->primary ? primary : clipboard).data.emplace_back(p->mime, std::move(p->data));
        }
        pipes.clear();
        auto d = std::move(done);
        Saved c = std::move(clipboard), pr = std::move(primary);
        deleteLater();
        d(std::move(c), std::move(pr));
    }
    void readable(Pipe* p) {
        char buf[65536];
        for (;;) {
            const ssize_t n = ::read(p->fd, buf, sizeof buf);
            if (n > 0) {
                p->data.append(buf, std::size_t(n));
                total += std::size_t(n);
                if (total > kMaxSaved)
                    return finish();
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EINTR))
                return;
            break;
        }
        p->notifier->setEnabled(false);
        close(p->fd);
        p->fd = -1;
        (p->primary ? primary : clipboard).data.emplace_back(p->mime, std::move(p->data));
        std::erase_if(pipes, [p](const auto& q) { return q.get() == p; });
        if (pipes.empty())
            finish();
    }
};

namespace {

const ext_data_control_offer_v1_listener kOfferListener = {
    .offer = [](void* data, ext_data_control_offer_v1* offer, const char* mime) {
        static_cast<Inserter*>(data)->onOfferMime(offer, mime);
    },
};

const ext_data_control_device_v1_listener kDeviceListener = {
    .data_offer = [](void* data, ext_data_control_device_v1*, ext_data_control_offer_v1* offer) {
        static_cast<Inserter*>(data)->onOffer(offer);
    },
    .selection = [](void* data, ext_data_control_device_v1*, ext_data_control_offer_v1* offer) {
        static_cast<Inserter*>(data)->onSelection(offer, false);
    },
    .finished = [](void*, ext_data_control_device_v1*) { qCWarning(lc) << "the compositor ended clipboard access"; },
    .primary_selection = [](void* data, ext_data_control_device_v1*, ext_data_control_offer_v1* offer) {
        static_cast<Inserter*>(data)->onSelection(offer, true);
    },
};

const ext_data_control_source_v1_listener kSourceListener = {
    .send = [](void* data, ext_data_control_source_v1* source, const char* mime, int32_t fd) {
        static_cast<Inserter*>(data)->onSend(source, mime, fd);
    },
    .cancelled = [](void* data, ext_data_control_source_v1* source) {
        static_cast<Inserter*>(data)->onCancelled(source);
    },
};

const zwlr_foreign_toplevel_handle_v1_listener kToplevelListener = {
    .title = [](void*, zwlr_foreign_toplevel_handle_v1*, const char*) {},
    .app_id = [](void* data, zwlr_foreign_toplevel_handle_v1* h, const char* id) {
        static_cast<Inserter*>(data)->toplevels_[h].appId = id;
    },
    .output_enter = [](void*, zwlr_foreign_toplevel_handle_v1*, wl_output*) {},
    .output_leave = [](void*, zwlr_foreign_toplevel_handle_v1*, wl_output*) {},
    .state = [](void* data, zwlr_foreign_toplevel_handle_v1* h, wl_array* states) {
        bool active = false;
        for (auto* s = static_cast<const std::uint32_t*>(states->data);
             reinterpret_cast<const char*>(s) < static_cast<const char*>(states->data) + states->size; ++s)
            active = active || *s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
        static_cast<Inserter*>(data)->toplevels_[h].activated = active;
    },
    .done = [](void*, zwlr_foreign_toplevel_handle_v1*) {},
    .closed = [](void* data, zwlr_foreign_toplevel_handle_v1* h) {
        static_cast<Inserter*>(data)->toplevels_.erase(h);
        zwlr_foreign_toplevel_handle_v1_destroy(h);
    },
    .parent = [](void*, zwlr_foreign_toplevel_handle_v1*, zwlr_foreign_toplevel_handle_v1*) {},
};

const zwlr_foreign_toplevel_manager_v1_listener kToplevelManagerListener = {
    .toplevel = [](void* data, zwlr_foreign_toplevel_manager_v1*, zwlr_foreign_toplevel_handle_v1* h) {
        static_cast<Inserter*>(data)->toplevels_[h] = {};
        zwlr_foreign_toplevel_handle_v1_add_listener(h, &kToplevelListener, data);
    },
    .finished = [](void*, zwlr_foreign_toplevel_manager_v1*) {},
};

const wl_registry_listener kRegistryListener = {
    .global = [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
        auto* self = static_cast<Inserter*>(data);
        if (!std::strcmp(interface, wl_seat_interface.name) && !self->seat_)
            self->seat_ = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
        else if (!std::strcmp(interface, ext_data_control_manager_v1_interface.name))
            self->dataManager_ = static_cast<ext_data_control_manager_v1*>(
                wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1));
        else if (!std::strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name))
            self->keyboardManager_ = static_cast<zwp_virtual_keyboard_manager_v1*>(
                wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
        else if (!std::strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name)) {
            self->toplevelManager_ = static_cast<zwlr_foreign_toplevel_manager_v1*>(
                wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface, std::min(version, 3u)));
            zwlr_foreign_toplevel_manager_v1_add_listener(self->toplevelManager_, &kToplevelManagerListener, self);
        }
    },
    .global_remove = [](void*, wl_registry*, uint32_t) {},
};

} // namespace

Inserter::Inserter(QObject* parent) : QObject(parent) {
    display_ = wl_display_connect(nullptr);
    if (!display_) {
        qCWarning(lc) << "no Wayland display";
        return;
    }
    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &kRegistryListener, this);
    wl_display_roundtrip(display_);
    if (seat_ && dataManager_) {
        device_ = ext_data_control_manager_v1_get_data_device(dataManager_, seat_);
        ext_data_control_device_v1_add_listener(device_, &kDeviceListener, this);
    }
    notifier_ = std::make_unique<QSocketNotifier>(wl_display_get_fd(display_), QSocketNotifier::Read);
    connect(notifier_.get(), &QSocketNotifier::activated, this, &Inserter::dispatch);
    wl_display_roundtrip(display_);
}

Inserter::~Inserter() {
    if (!display_)
        return;
    for (auto& [source, content] : sources_)
        ext_data_control_source_v1_destroy(source);
    for (const Offer& o : offers_)
        ext_data_control_offer_v1_destroy(o.offer);
    for (auto& [h, t] : toplevels_)
        zwlr_foreign_toplevel_handle_v1_destroy(h);
    if (keyboard_)
        zwp_virtual_keyboard_v1_destroy(keyboard_);
    if (device_)
        ext_data_control_device_v1_destroy(device_);
    wl_display_disconnect(display_);
}

QString Inserter::missing() const {
    if (!display_)
        return tr("no Wayland display");
    if (!dataManager_)
        return tr("the desktop has no clipboard access for apps (ext-data-control-v1)");
    if (!keyboardManager_)
        return tr("the desktop has no virtual keyboard (zwp_virtual_keyboard_v1)");
    return {};
}

QString Inserter::focusedApp() const {
    for (const auto& [h, t] : toplevels_)
        if (t.activated)
            return QString::fromStdString(t.appId);
    return {};
}

void Inserter::dispatch() {
    if (wl_display_dispatch(display_) < 0) {
        qCWarning(lc) << "lost the Wayland connection";
        notifier_->setEnabled(false);
        QCoreApplication::exit(1);
        return;
    }
    flush();
}

void Inserter::flush() {
    wl_display_flush(display_);
}

Inserter::Offer* Inserter::find(ext_data_control_offer_v1* offer) {
    for (Offer& o : offers_)
        if (o.offer == offer)
            return &o;
    return nullptr;
}

void Inserter::onOffer(ext_data_control_offer_v1* offer) {
    offers_.push_back({offer, {}});
    ext_data_control_offer_v1_add_listener(offer, &kOfferListener, this);
}

void Inserter::onOfferMime(ext_data_control_offer_v1* offer, const char* mime) {
    if (Offer* o = find(offer))
        o->mimes.emplace_back(mime);
}

void Inserter::onSelection(ext_data_control_offer_v1* offer, bool primary) {
    ext_data_control_offer_v1*& slot = primary ? primary_ : selection_;
    ext_data_control_offer_v1* old = std::exchange(slot, offer);
    // An offer neither selection holds any more is done with.
    if (old && old != selection_ && old != primary_) {
        std::erase_if(offers_, [old](const Offer& o) { return o.offer == old; });
        ext_data_control_offer_v1_destroy(old);
    }
}

void Inserter::onSend(ext_data_control_source_v1* source, const char* mime, int fd) {
    auto it = sources_.find(source);
    std::string data;
    if (it != sources_.end())
        for (const auto& [m, d] : it->second->data)
            if (m == mime) {
                data = d;
                break;
            }
    if (paste_ && (source == clipboardSource_ || source == primarySource_) && !paste_->read) {
        paste_->read = true;
        QTimer::singleShot(kReadGraceMs, this, &Inserter::restore);
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    auto* w = new Source(fd, std::move(data));
    w->setParent(this);
    w->pump();
}

void Inserter::onCancelled(ext_data_control_source_v1* source) {
    sources_.erase(source);
    if (clipboardSource_ == source)
        clipboardSource_ = nullptr;
    if (primarySource_ == source)
        primarySource_ = nullptr;
    ext_data_control_source_v1_destroy(source);
}

ext_data_control_source_v1* Inserter::makeSource(std::shared_ptr<const Saved> content, bool primary) {
    if (content->data.empty()) {
        if (primary)
            ext_data_control_device_v1_set_primary_selection(device_, nullptr);
        else
            ext_data_control_device_v1_set_selection(device_, nullptr);
        return nullptr;
    }
    auto* source = ext_data_control_manager_v1_create_data_source(dataManager_);
    ext_data_control_source_v1_add_listener(source, &kSourceListener, this);
    for (const auto& [mime, data] : content->data)
        ext_data_control_source_v1_offer(source, mime.c_str());
    sources_[source] = std::move(content);
    if (primary)
        ext_data_control_device_v1_set_primary_selection(device_, source);
    else
        ext_data_control_device_v1_set_selection(device_, source);
    return source;
}

void Inserter::snapshot(std::function<void(Saved, Saved)> done) {
    auto* snap = new Snapshot;
    snap->setParent(this);
    snap->done = std::move(done);
    auto take = [&](ext_data_control_offer_v1* offer, ext_data_control_source_v1* ours, bool primary) {
        // Our own source still up: its content is right here.
        if (ours) {
            if (auto it = sources_.find(ours); it != sources_.end())
                (primary ? snap->primary : snap->clipboard) = *it->second;
            return;
        }
        Offer* o = offer ? find(offer) : nullptr;
        if (!o)
            return;
        for (const std::string& mime : o->mimes) {
            if (std::ranges::contains(kMetaMimes, std::string_view(mime)))
                continue;
            int fds[2];
            if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) < 0)
                continue;
            ext_data_control_offer_v1_receive(offer, mime.c_str(), fds[1]);
            close(fds[1]);
            auto p = std::make_unique<Snapshot::Pipe>();
            p->fd = fds[0];
            p->primary = primary;
            p->mime = mime;
            Snapshot::Pipe* raw = p.get();
            p->notifier = std::make_unique<QSocketNotifier>(fds[0], QSocketNotifier::Read);
            connect(p->notifier.get(), &QSocketNotifier::activated, snap, [snap, raw] { snap->readable(raw); });
            snap->pipes.push_back(std::move(p));
        }
    };
    take(selection_, clipboardSource_, false);
    take(primary_, primarySource_, true);
    flush();
    if (snap->pipes.empty()) {
        snap->finish();
        return;
    }
    // An app that never answers doesn't hold up the paste.
    snap->deadline.setSingleShot(true);
    connect(&snap->deadline, &QTimer::timeout, snap, [snap] { snap->finish(); });
    snap->deadline.start(300);
}

bool Inserter::ensureKeyboard() {
    if (keyboard_)
        return true;
    if (!keyboardManager_ || !seat_)
        return false;
    // The standard US layout: Shift and Insert are where every layout has them.
    xkb_context* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    const xkb_rule_names names{"evdev", "pc105", "us", "", ""};
    xkb_keymap* km = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!km) {
        xkb_context_unref(ctx);
        return false;
    }
    shiftMask_ = 1u << xkb_keymap_mod_get_index(km, XKB_MOD_NAME_SHIFT);
    char* text = xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1);
    const std::size_t size = std::strlen(text) + 1;
    const int fd = memfd_create("tala-keymap", MFD_CLOEXEC);
    bool ok = fd >= 0 && ftruncate(fd, off_t(size)) == 0;
    if (ok) {
        void* map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ok = map != MAP_FAILED;
        if (ok) {
            std::memcpy(map, text, size);
            munmap(map, size);
        }
    }
    std::free(text);
    xkb_keymap_unref(km);
    xkb_context_unref(ctx);
    if (!ok) {
        if (fd >= 0)
            close(fd);
        return false;
    }
    keyboard_ = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(keyboardManager_, seat_);
    zwp_virtual_keyboard_v1_keymap(keyboard_, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, std::uint32_t(size));
    close(fd);
    wl_display_roundtrip(display_);
    return true;
}

void Inserter::pressPaste() {
    const std::uint32_t t = now();
    auto key = [this, t](std::uint32_t code, bool down) {
        zwp_virtual_keyboard_v1_key(keyboard_, t, code, down ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
    };
    key(KEY_LEFTSHIFT, true);
    zwp_virtual_keyboard_v1_modifiers(keyboard_, shiftMask_, 0, 0, 0);
    key(KEY_INSERT, true);
    key(KEY_INSERT, false);
    key(KEY_LEFTSHIFT, false);
    zwp_virtual_keyboard_v1_modifiers(keyboard_, 0, 0, 0, 0);
    flush();
}

void Inserter::copy(const QString& text) {
    if (!device_)
        return;
    clipboardSource_ = makeSource(std::make_shared<Saved>(textContent(text)), false);
    flush();
}

void Inserter::insert(const QString& text, std::function<void(bool)> done) {
    if (!missing().isEmpty() || !device_ || !ensureKeyboard()) {
        copy(text);
        done(false);
        return;
    }
    // One at a time: a paste still waiting to restore is finished first.
    if (paste_)
        restore();
    paste_ = std::make_unique<Paste>();
    paste_->done = std::move(done);
    snapshot([this, text](Saved clipboard, Saved primary) {
        if (!paste_)
            return;
        paste_->clipboard = std::move(clipboard);
        paste_->primary = std::move(primary);
        auto content = std::make_shared<const Saved>(textContent(text));
        clipboardSource_ = makeSource(content, false);
        primarySource_ = makeSource(content, true);
        // The compositor has the new selections before the key arrives.
        wl_display_roundtrip(display_);
        pressPaste();
        QTimer::singleShot(kNoReadMs, this, [this, p = paste_.get()] {
            if (paste_.get() == p && !p->read)
                restore();
        });
    });
}

void Inserter::restore() {
    if (!paste_)
        return;
    std::unique_ptr<Paste> p = std::move(paste_);
    // Something copied since then stays; only our own text is replaced.
    if (clipboardSource_)
        clipboardSource_ = makeSource(std::make_shared<const Saved>(std::move(p->clipboard)), false);
    if (primarySource_)
        primarySource_ = makeSource(std::make_shared<const Saved>(std::move(p->primary)), true);
    flush();
    if (p->done)
        p->done(p->read);
}

} // namespace tala
