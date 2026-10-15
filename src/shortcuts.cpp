#include "shortcuts.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

namespace tala {

namespace {

Q_LOGGING_CATEGORY(lc, "tala.shortcuts")

constexpr const char* kService = "org.freedesktop.portal.Desktop";
constexpr const char* kPath = "/org/freedesktop/portal/desktop";
constexpr const char* kIface = "org.freedesktop.portal.GlobalShortcuts";
constexpr const char* kRequest = "org.freedesktop.portal.Request";
// The app id the portal knows tala by (and the .desktop file's name).
constexpr const char* kAppId = "io.github.mkonic.Tala";

struct PortalShortcut {
    QString id;
    QVariantMap options;
};
using PortalShortcuts = QList<PortalShortcut>;

QDBusArgument& operator<<(QDBusArgument& a, const PortalShortcut& s) {
    a.beginStructure();
    a << s.id << s.options;
    a.endStructure();
    return a;
}

const QDBusArgument& operator>>(const QDBusArgument& a, PortalShortcut& s) {
    a.beginStructure();
    a >> s.id >> s.options;
    a.endStructure();
    return a;
}

} // namespace
} // namespace tala

Q_DECLARE_METATYPE(tala::PortalShortcut)
Q_DECLARE_METATYPE(tala::PortalShortcuts)

namespace tala {

Shortcuts::Shortcuts(QObject* parent) : QObject(parent) {
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<PortalShortcuts>();
}

QString Shortcuts::requestPath(const QString& token) const {
    QString sender = QDBusConnection::sessionBus().baseService().mid(1);
    sender.replace('.', '_');
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}

void Shortcuts::bind(const QList<Action>& actions) {
    actions_ = actions;
    QDBusConnection bus = QDBusConnection::sessionBus();
    // Unsandboxed, the portal can't tell who's asking: say so, first.
    QDBusMessage reg = QDBusMessage::createMethodCall(kService, kPath, "org.freedesktop.host.portal.Registry", "Register");
    reg << QString::fromLatin1(kAppId) << QVariantMap{};
    if (const QDBusMessage r = bus.call(reg, QDBus::Block, 2000); r.type() == QDBusMessage::ErrorMessage)
        qCWarning(lc) << "registering the app id:" << r.errorMessage();

    const QString token = QStringLiteral("tala%1").arg(++tokens_);
    bus.connect(kService, requestPath(token), kRequest, "Response", this, SLOT(onSessionResponse(uint, QVariantMap)));
    QDBusMessage m = QDBusMessage::createMethodCall(kService, kPath, kIface, "CreateSession");
    m << QVariantMap{{"handle_token", token}, {"session_handle_token", QStringLiteral("tala_session")}};
    auto* w = new QDBusPendingCallWatcher(bus.asyncCall(m), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (w->isError()) {
            qCWarning(lc) << w->error().message();
            emit failed(w->error().message());
        }
    });
}

void Shortcuts::onSessionResponse(uint code, const QVariantMap& results) {
    qCInfo(lc) << "session" << code << results;
    if (code != 0) {
        emit failed(tr("the desktop declined global shortcuts"));
        return;
    }
    session_ = results.value("session_handle").toString();
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(kService, kPath, kIface, "Activated", this,
                SLOT(onActivated(QDBusObjectPath, QString, qulonglong, QVariantMap)));
    bus.connect(kService, kPath, kIface, "Deactivated", this,
                SLOT(onDeactivated(QDBusObjectPath, QString, qulonglong, QVariantMap)));
    bus.connect(kService, kPath, kIface, "ShortcutsChanged", this,
                SLOT(onShortcutsChanged(QDBusMessage)));

    PortalShortcuts list;
    for (const Action& a : actions_)
        list.append({a.id, {{"description", a.description}, {"preferred_trigger", a.preferred}}});
    const QString token = QStringLiteral("tala%1").arg(++tokens_);
    bus.connect(kService, requestPath(token), kRequest, "Response", this, SLOT(onBindResponse(uint, QVariantMap)));
    QDBusMessage m = QDBusMessage::createMethodCall(kService, kPath, kIface, "BindShortcuts");
    m << QVariant::fromValue(QDBusObjectPath(session_)) << QVariant::fromValue(list) << QString()
      << QVariantMap{{"handle_token", token}};
    auto* w = new QDBusPendingCallWatcher(bus.asyncCall(m), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (w->isError()) {
            qCWarning(lc) << w->error().message();
            emit failed(w->error().message());
        }
    });
}

void Shortcuts::readShortcuts(const QDBusArgument& arg) {
    PortalShortcuts list;
    arg >> list;
    triggers_.clear();
    for (const PortalShortcut& s : list)
        triggers_.insert(s.id, s.options.value("trigger_description").toString());
    emit triggersChanged();
}

void Shortcuts::onBindResponse(uint code, const QVariantMap& results) {
    qCInfo(lc) << "bound" << code << results.keys();
    if (code != 0) {
        emit failed(tr("the desktop didn't bind tala's shortcuts"));
        return;
    }
    available_ = true;
    const QVariant v = results.value("shortcuts");
    if (v.canConvert<QDBusArgument>())
        readShortcuts(v.value<QDBusArgument>());
    emit ready();
}

void Shortcuts::onActivated(const QDBusObjectPath& session, const QString& id, qulonglong, const QVariantMap&) {
    if (session.path() == session_)
        emit pressed(id);
}

void Shortcuts::onDeactivated(const QDBusObjectPath& session, const QString& id, qulonglong, const QVariantMap&) {
    if (session.path() == session_)
        emit released(id);
}

void Shortcuts::onShortcutsChanged(const QDBusMessage& message) {
    const QList<QVariant> args = message.arguments();
    if (args.size() == 2 && args[0].value<QDBusObjectPath>().path() == session_)
        readShortcuts(args[1].value<QDBusArgument>());
}

void Shortcuts::configure() {
    if (session_.isEmpty())
        return;
    QDBusMessage m = QDBusMessage::createMethodCall(kService, kPath, kIface, "ConfigureShortcuts");
    m << QVariant::fromValue(QDBusObjectPath(session_)) << QString() << QVariantMap{};
    QDBusConnection::sessionBus().asyncCall(m);
}

} // namespace tala
