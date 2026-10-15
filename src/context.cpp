#include "context.hpp"

#include <QDBusReply>
#include <QDBusVariant>
#include <QLoggingCategory>

namespace tala {

namespace {

Q_LOGGING_CATEGORY(lc, "tala.context")

constexpr const char* kFocused = "object:state-changed:focused";
constexpr int kTimeoutMs = 150;  // a field that doesn't answer by then is guessed at

} // namespace

Context::Context(QObject* parent) : QObject(parent) {}

bool Context::connectBus() {
    if (connected_)
        return true;
    QDBusMessage get = QDBusMessage::createMethodCall("org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress");
    const QDBusReply<QString> address = QDBusConnection::sessionBus().call(get, QDBus::Block, 1000);
    if (!address.isValid() || address.value().isEmpty()) {
        qCWarning(lc) << "no accessibility bus:" << address.error().message();
        return false;
    }
    QDBusConnection bus = QDBusConnection::connectToBus(address.value(), busName_);
    if (!bus.isConnected()) {
        qCWarning(lc) << "can't reach the accessibility bus";
        return false;
    }
    bus.connect({}, {}, "org.a11y.atspi.Event.Object", "StateChanged", this, SLOT(onStateChanged(QDBusMessage)));
    QDBusMessage reg = QDBusMessage::createMethodCall("org.a11y.atspi.Registry", "/org/a11y/atspi/registry",
                                                      "org.a11y.atspi.Registry", "RegisterEvent");
    reg << QString::fromLatin1(kFocused) << QStringList{} << QString();
    bus.asyncCall(reg);
    connected_ = true;
    return true;
}

void Context::setEnabled(bool on) {
    if (on == enabled_)
        return;
    enabled_ = on;
    if (!on) {
        if (connected_) {
            QDBusConnection bus(busName_);
            QDBusMessage dereg = QDBusMessage::createMethodCall("org.a11y.atspi.Registry", "/org/a11y/atspi/registry",
                                                                "org.a11y.atspi.Registry", "DeregisterEvent");
            dereg << QString::fromLatin1(kFocused) << QString();
            bus.asyncCall(dereg);
            bus.disconnect({}, {}, "org.a11y.atspi.Event.Object", "StateChanged", this,
                           SLOT(onStateChanged(QDBusMessage)));
            QDBusConnection::disconnectFromBus(busName_);
            connected_ = false;
        }
        service_.clear();
        path_.clear();
        return;
    }
    // Apps start answering once the session's accessibility is on.
    QDBusMessage set = QDBusMessage::createMethodCall("org.a11y.Bus", "/org/a11y/bus", "org.freedesktop.DBus.Properties",
                                                      "Set");
    set << QString("org.a11y.Status") << QString("IsEnabled") << QVariant::fromValue(QDBusVariant(true));
    QDBusConnection::sessionBus().asyncCall(set);
    connectBus();
}

void Context::onStateChanged(const QDBusMessage& message) {
    const QList<QVariant> args = message.arguments();
    if (args.size() < 2 || args[0].toString() != QLatin1String("focused"))
        return;
    if (args[1].toInt() == 1) {
        service_ = message.service();
        path_ = message.path();
    } else if (message.service() == service_ && message.path() == path_) {
        service_.clear();
        path_.clear();
    }
}

std::optional<QString> Context::beforeCaret(int chars) const {
    if (!enabled_ || !connected_ || service_.isEmpty())
        return std::nullopt;
    QDBusConnection bus(busName_);
    QDBusMessage caret = QDBusMessage::createMethodCall(service_, path_, "org.freedesktop.DBus.Properties", "Get");
    caret << QString("org.a11y.atspi.Text") << QString("CaretOffset");
    const QDBusMessage c = bus.call(caret, QDBus::Block, kTimeoutMs);
    if (c.type() != QDBusMessage::ReplyMessage || c.arguments().isEmpty())
        return std::nullopt;
    const int offset = c.arguments()[0].value<QDBusVariant>().variant().toInt();
    if (offset < 0)
        return std::nullopt;
    QDBusMessage text = QDBusMessage::createMethodCall(service_, path_, "org.a11y.atspi.Text", "GetText");
    text << std::max(0, offset - chars) << offset;
    const QDBusMessage t = bus.call(text, QDBus::Block, kTimeoutMs);
    if (t.type() != QDBusMessage::ReplyMessage || t.arguments().isEmpty())
        return std::nullopt;
    return t.arguments()[0].toString();
}

} // namespace tala
