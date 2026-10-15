#include "screenshot.hpp"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QUrl>

namespace tala {

namespace {

constexpr const char* kService = "org.freedesktop.portal.Desktop";
constexpr const char* kPath = "/org/freedesktop/portal/desktop";

} // namespace

Screenshot::Screenshot(QObject* parent) : QObject(parent) {}

void Screenshot::take(Done done) {
    if (done_) {  // one at a time
        done({});
        return;
    }
    done_ = std::move(done);
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString token = QStringLiteral("tala_shot%1").arg(++tokens_);
    QString sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    bus.connect(kService, QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token),
                "org.freedesktop.portal.Request", "Response", this, SLOT(onResponse(uint, QVariantMap)));
    QDBusMessage m = QDBusMessage::createMethodCall(kService, kPath, "org.freedesktop.portal.Screenshot", "Screenshot");
    m << QString() << QVariantMap{{"handle_token", token}, {"interactive", false}};
    const QDBusMessage r = bus.call(m, QDBus::Block, 2000);
    if (r.type() == QDBusMessage::ErrorMessage) {
        Done d = std::move(done_);
        done_ = nullptr;
        d({});
    }
}

void Screenshot::onResponse(uint code, const QVariantMap& results) {
    if (!done_)
        return;
    Done d = std::move(done_);
    done_ = nullptr;
    d(code == 0 ? QUrl(results.value("uri").toString()).toLocalFile() : QString());
}

} // namespace tala
