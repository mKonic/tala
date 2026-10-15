#pragma once
// Puts text where the user is typing. Wayland has no "type this string into
// the focused field" for a client, so it goes the way Wispr Flow does on
// every platform: the text on the clipboard (and the primary selection),
// a paste key from a virtual keyboard, then the old clipboard back.
//
// Shift+Insert is the paste key: GTK, Qt, Chromium, Firefox and Electron
// paste the clipboard on it, and terminals (foot, Ghostty, kitty, Alacritty)
// paste the primary selection, which holds the same text. One key for both,
// and input methods (fcitx5, Mozc) let it through untouched, which typed
// characters would not be.
//
// Its own Wayland connection, beside Qt's: ext-data-control-v1,
// zwp_virtual_keyboard_v1, and zwlr_foreign_toplevel_management_v1 for the
// focused app's id.

#include <QObject>
#include <QSocketNotifier>
#include <QString>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct wl_display;
struct wl_registry;
struct wl_seat;
struct ext_data_control_manager_v1;
struct ext_data_control_device_v1;
struct ext_data_control_offer_v1;
struct ext_data_control_source_v1;
struct zwp_virtual_keyboard_manager_v1;
struct zwp_virtual_keyboard_v1;
struct zwlr_foreign_toplevel_manager_v1;
struct zwlr_foreign_toplevel_handle_v1;

namespace tala {

class Inserter : public QObject {
    Q_OBJECT

public:
    explicit Inserter(QObject* parent = nullptr);
    ~Inserter() override;

    // What's missing ("" when everything is there).
    QString missing() const;
    // The focused window's app id, when the desktop says.
    QString focusedApp() const;

    // Pastes `text` into the focused field. `done(false)` when it couldn't
    // (the text is then left on the clipboard).
    void insert(const QString& text, std::function<void(bool)> done);
    // Just the clipboard.
    void copy(const QString& text);

    // Callbacks from the protocol listeners.
    struct Offer {
        ext_data_control_offer_v1* offer = nullptr;
        std::vector<std::string> mimes;
    };
    struct Saved {
        std::vector<std::pair<std::string, std::string>> data;  // mime, bytes
    };
    struct Toplevel {
        std::string appId;
        bool activated = false;
    };
    void onOffer(ext_data_control_offer_v1* offer);
    void onOfferMime(ext_data_control_offer_v1* offer, const char* mime);
    void onSelection(ext_data_control_offer_v1* offer, bool primary);
    void onSend(ext_data_control_source_v1* source, const char* mime, int fd);
    void onCancelled(ext_data_control_source_v1* source);

    wl_seat* seat_ = nullptr;
    ext_data_control_manager_v1* dataManager_ = nullptr;
    zwp_virtual_keyboard_manager_v1* keyboardManager_ = nullptr;
    zwlr_foreign_toplevel_manager_v1* toplevelManager_ = nullptr;
    std::map<zwlr_foreign_toplevel_handle_v1*, Toplevel> toplevels_;

private:
    struct Source;
    struct Snapshot;

    void dispatch();
    void flush();
    Offer* find(ext_data_control_offer_v1* offer);
    ext_data_control_source_v1* makeSource(std::shared_ptr<const Saved> content, bool primary);
    void snapshot(std::function<void(Saved clipboard, Saved primary)> done);
    void pressPaste();
    void restore();
    bool ensureKeyboard();

    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    ext_data_control_device_v1* device_ = nullptr;
    zwp_virtual_keyboard_v1* keyboard_ = nullptr;
    std::uint32_t shiftMask_ = 0;
    std::unique_ptr<QSocketNotifier> notifier_;

    std::vector<Offer> offers_;
    ext_data_control_offer_v1* selection_ = nullptr;  // the clipboard now
    ext_data_control_offer_v1* primary_ = nullptr;    // the primary selection now
    // Ours, while they hold the selections: what each offers.
    std::map<ext_data_control_source_v1*, std::shared_ptr<const Saved>> sources_;
    ext_data_control_source_v1* clipboardSource_ = nullptr;
    ext_data_control_source_v1* primarySource_ = nullptr;

    // The paste in flight.
    struct Paste {
        Saved clipboard, primary;
        bool read = false;  // the app took the text
        std::function<void(bool)> done;
    };
    std::unique_ptr<Paste> paste_;
};

} // namespace tala
