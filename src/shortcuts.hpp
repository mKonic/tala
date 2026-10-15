#pragma once
// tala's keys, held by the desktop through the GlobalShortcuts portal
// (org.freedesktop.portal.GlobalShortcuts): the desktop owns the bindings and
// lets the user change them in its own settings; tala hears press and release.

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace tala {

class Shortcuts : public QObject {
    Q_OBJECT

public:
    struct Action {
        QString id;
        QString description;
        QString preferred;  // the portal's spelling: "CTRL+LOGO+space"
    };

    explicit Shortcuts(QObject* parent = nullptr);

    // Opens a session and binds `actions`. Asynchronous; ready() follows.
    void bind(const QList<Action>& actions);
    bool available() const { return available_; }
    // id -> how the desktop says its keys ("Ctrl+Super+Space").
    QVariantMap triggers() const { return triggers_; }
    // The desktop's own place to change them.
    void configure();
    // Escape cancels while dictating: bound in a session of its own, open
    // only then (Flow's). Only on atrium, which takes a shortcut's key only
    // while it is bound; elsewhere Escape would be taken for good, or the
    // desktop would ask about it every time.
    void setEscape(bool on);
    static bool escapeWorks();

signals:
    void pressed(const QString& id);
    // `cancelled`: another key joined a modifier-only chord (atrium's hint);
    // what the hold began is better dropped.
    void released(const QString& id, bool cancelled);
    void ready();
    void failed(const QString& why);
    void triggersChanged();

private slots:
    void onSessionResponse(uint code, const QVariantMap& results);
    void onBindResponse(uint code, const QVariantMap& results);
    void onActivated(const QDBusObjectPath& session, const QString& id, qulonglong time, const QVariantMap& options);
    void onDeactivated(const QDBusObjectPath& session, const QString& id, qulonglong time, const QVariantMap& options);
    void onShortcutsChanged(const QDBusMessage& message);
    void onEscapeSession(uint code, const QVariantMap& results);

private:
    QDBusConnection bus() const;
    QString requestPath(const QString& token) const;
    void readShortcuts(const QDBusArgument& arg);

    QList<Action> actions_;
    QString session_;
    QString escape_;  // its session while dictating
    bool escapeWanted_ = false, escapePending_ = false;
    QVariantMap triggers_;
    bool available_ = false;
    int tokens_ = 0;
};

} // namespace tala
