#pragma once
// tala's keys, held by the desktop through the GlobalShortcuts portal
// (org.freedesktop.portal.GlobalShortcuts): the desktop owns the bindings and
// lets the user change them in its own settings; tala hears press and release.

#include <QDBusArgument>
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

signals:
    void pressed(const QString& id);
    void released(const QString& id);
    void ready();
    void failed(const QString& why);
    void triggersChanged();

private slots:
    void onSessionResponse(uint code, const QVariantMap& results);
    void onBindResponse(uint code, const QVariantMap& results);
    void onActivated(const QDBusObjectPath& session, const QString& id, qulonglong time, const QVariantMap& options);
    void onDeactivated(const QDBusObjectPath& session, const QString& id, qulonglong time, const QVariantMap& options);
    void onShortcutsChanged(const QDBusMessage& message);

private:
    QString requestPath(const QString& token) const;
    void readShortcuts(const QDBusArgument& arg);

    QList<Action> actions_;
    QString session_;
    QVariantMap triggers_;
    bool available_ = false;
    int tokens_ = 0;
};

} // namespace tala
