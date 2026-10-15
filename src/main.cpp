// tala: dictation for the Linux desktop. Hold a key, speak, and the text is
// typed where the cursor is. Phonon-2 does the listening, on this machine.

#include "controller.hpp"
#include "store.hpp"
#include "ui.hpp"
#include "version.hpp"

#include <LayerShellQt/window.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QStandardPaths>
#include <QSystemTrayIcon>

#include <QSocketNotifier>

#include <csignal>
#include <sys/signalfd.h>
#include <unistd.h>

using namespace tala;

namespace {

QString instanceName() {
    return QStringLiteral("tala-%1").arg(getuid());
}

// The bar: a layer surface at the bottom of the screen that never takes the
// keyboard, so the field being dictated into keeps its focus.
void makeBar(QQuickWindow* w) {
    auto* layer = LayerShellQt::Window::get(w);
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setAnchors(LayerShellQt::Window::AnchorBottom);
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setExclusiveZone(0);
    layer->setMargins({0, 0, 0, 14});
    layer->setScope(QStringLiteral("tala-bar"));
    layer->setActivateOnShow(false);
    layer->setWantsToBeOnActiveScreen(true);
}

} // namespace

int main(int argc, char** argv) {
    // SIGTERM and SIGINT end the event loop, so the engine and the clipboard
    // are put away properly.
    sigset_t quitSignals;
    sigemptyset(&quitSignals);
    sigaddset(&quitSignals, SIGTERM);
    sigaddset(&quitSignals, SIGINT);
    pthread_sigmask(SIG_BLOCK, &quitSignals, nullptr);

    // Before the application exists: Qt's portal theme registers the app id
    // with the portal while it starts, and only the first registration counts.
    QGuiApplication::setDesktopFileName("io.github.mkonic.Tala");
    QApplication app(argc, argv);
    QSocketNotifier quitNotifier(signalfd(-1, &quitSignals, SFD_CLOEXEC | SFD_NONBLOCK), QSocketNotifier::Read);
    QObject::connect(&quitNotifier, &QSocketNotifier::activated, &app, &QApplication::quit);
    app.setApplicationName("tala");
    app.setApplicationDisplayName("Tala");
    app.setApplicationVersion(TALA_VERSION);
    app.setQuitOnLastWindowClosed(false);
    const QIcon icon(QStringLiteral(":/tala/icons/tala.svg"));
    app.setWindowIcon(QIcon::fromTheme("io.github.mkonic.Tala", icon));

    QCommandLineParser args;
    args.setApplicationDescription("Dictation: hold a key, speak, and the text is typed where the cursor is.");
    args.addHelpOption();
    args.addVersionOption();
    QCommandLineOption background("background", "Start without opening the window (for login).");
    args.addOption(background);
    args.process(app);

    // One tala per user: a second start just opens the first one's window.
    {
        QLocalSocket other;
        other.connectToServer(instanceName());
        if (other.waitForConnected(300)) {
            other.write("show\n");
            other.waitForBytesWritten(300);
            return 0;
        }
    }
    QLocalServer::removeServer(instanceName());
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    server.listen(instanceName());

    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/tala";
    QDir().mkpath(data);
    Store store((data + "/tala.db").toStdString());
    if (!store.ok())
        return 1;
    Controller controller(store);

    // tala draws its own controls; Basic is the style that doesn't fight it.
    QQuickStyle::setStyle("Basic");
    QQmlApplicationEngine engine;
    Ui ui;
    engine.rootContext()->setContextProperty("tala", &controller);
    engine.rootContext()->setContextProperty("ui", &ui);
    engine.load(QUrl("qrc:/tala/Bar.qml"));
    auto* bar = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!bar)
        return 1;
    makeBar(bar);
    bar->show();
    // A layer surface stays on the output it was made on: made again on the
    // screen being dictated on, so the bar and its notices are in sight.
    QObject::connect(&controller, &tala::Controller::screenWanted, bar, [bar](const QString& name) {
        auto* layer = LayerShellQt::Window::get(bar);
        QScreen* now = layer->screen() ? layer->screen() : bar->screen();
        for (QScreen* s : QGuiApplication::screens())
            if (s->name() == name && s != now) {
                layer->setWantsToBeOnActiveScreen(false);
                layer->setScreen(s);
                bar->setScreen(s);
                bar->hide();
                bar->show();
                return;
            }
    });

    QQuickWindow* hub = nullptr;
    auto showHub = [&] {
        if (!hub) {
            const qsizetype before = engine.rootObjects().size();
            engine.load(QUrl("qrc:/tala/Hub.qml"));
            if (engine.rootObjects().size() > before)
                hub = qobject_cast<QQuickWindow*>(engine.rootObjects().last());
        }
        if (hub) {
            hub->show();
            hub->raise();
            hub->requestActivate();
        }
    };
    QObject::connect(&controller, &Controller::showHub, &app, showHub);
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
        while (QLocalSocket* s = server.nextPendingConnection()) {
            QObject::connect(s, &QLocalSocket::readyRead, s, [s, &showHub] {
                if (s->readAll().startsWith("show"))
                    showHub();
                s->deleteLater();
            });
        }
    });

    QSystemTrayIcon tray(app.windowIcon());
    QMenu menu;
    menu.addAction(QObject::tr("Open Tala"), showHub);
    menu.addAction(QObject::tr("Paste last transcript"), &controller, &Controller::pasteLast);
    menu.addSeparator();
    menu.addAction(QObject::tr("Quit"), &app, &QApplication::quit);
    tray.setContextMenu(&menu);
    tray.setToolTip("Tala");
    QObject::connect(&tray, &QSystemTrayIcon::activated, &app, [&](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger)
            showHub();
    });
    tray.show();

    if (!args.isSet(background))
        showHub();
    return app.exec();
}
