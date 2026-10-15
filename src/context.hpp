#pragma once
// The text before the caret, read through AT-SPI (the desktop's
// accessibility bus), so a dictation into the middle of a sentence fits it:
// Flow's context awareness. It follows which widget has focus and asks that
// one, when asked, for its caret and the text just before it.
//
// Apps only speak AT-SPI while the session's accessibility is on
// (org.a11y.Status.IsEnabled); turning context on turns that on, which is
// why it's the user's choice (Settings).

#include <QDBusConnection>
#include <QDBusMessage>
#include <QObject>
#include <QString>

#include <optional>

namespace tala {

class Context : public QObject {
    Q_OBJECT

public:
    explicit Context(QObject* parent = nullptr);

    // Starts or stops following focus (and turns the session's
    // accessibility on when starting).
    void setEnabled(bool on);
    bool enabled() const { return enabled_; }

    // Up to `chars` of text before the caret in the focused text field;
    // nothing when it isn't one, or doesn't say (then tala guesses).
    std::optional<QString> beforeCaret(int chars = 200) const;

private slots:
    void onStateChanged(const QDBusMessage& message);

private:
    bool connectBus();

    bool enabled_ = false;
    bool connected_ = false;
    QString busName_ = QStringLiteral("tala-a11y");
    // The focused accessible: its owner on the bus and its object path.
    QString service_, path_;
};

} // namespace tala
