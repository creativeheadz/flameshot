#include "systemnotification.h"
#include "core/flameshotdaemon.h"
#include "utils/abstractlogger.h"
#include "utils/confighandler.h"
#include "utils/filemanagerutils.h"

#include <QApplication>
#include <QUrl>
#if !(defined(Q_OS_MACOS) || defined(Q_OS_WIN))
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#endif

// work-around for snap, which cannot install icons into
// the system folder, so instead the absolute path to the
// icon (saved somewhere in /snap/flameshot/...) is passed
#ifndef FLAMESHOT_ICON
#define FLAMESHOT_ICON "flameshot"
#endif

namespace {

// Identifier of the notification button, sent to the notification server and
// handed back to us when the button is pressed.
const auto OPEN_FOLDER_ACTION = QStringLiteral("flameshot-open-folder");

// Notifications whose button is never going to be pressed still take up a slot
// in the handler, in case the notification server does not tell us they are
// gone. Forget about the oldest ones once there are clearly too many.
constexpr int MAX_TRACKED_NOTIFICATIONS = 64;

} // namespace

NotificationActionHandler* NotificationActionHandler::instance()
{
    static auto* handler = new NotificationActionHandler(qApp);
    return handler;
}

NotificationActionHandler::NotificationActionHandler(QObject* parent)
  : QObject(parent)
{
#if !(defined(Q_OS_MACOS) || defined(Q_OS_WIN))
    auto bus = QDBusConnection::sessionBus();
    auto service = QStringLiteral("org.freedesktop.Notifications");
    auto path = QStringLiteral("/org/freedesktop/Notifications");
    bus.connect(service,
                path,
                service,
                QStringLiteral("ActionInvoked"),
                this,
                SLOT(onActionInvoked(uint, QString)));
    bus.connect(service,
                path,
                service,
                QStringLiteral("NotificationClosed"),
                this,
                SLOT(onNotificationClosed(uint, uint)));
#endif
}

void NotificationActionHandler::trackNotification(uint notificationId,
                                                  const QString& savePath)
{
    if (m_savePaths.size() >= MAX_TRACKED_NOTIFICATIONS) {
        m_savePaths.clear();
    }
    m_savePaths.insert(notificationId, savePath);
}

void NotificationActionHandler::onActionInvoked(uint notificationId,
                                                const QString& actionKey)
{
    if (actionKey != OPEN_FOLDER_ACTION) {
        return;
    }
    const QString savePath = m_savePaths.value(notificationId);
    if (!savePath.isEmpty()) {
        FileManagerUtils::revealFile(savePath);
    }
}

void NotificationActionHandler::onNotificationClosed(uint notificationId,
                                                     uint reason)
{
    Q_UNUSED(reason)
    m_savePaths.remove(notificationId);
}

SystemNotification::SystemNotification(QObject* parent)
  : QObject(parent)
  , m_interface(nullptr)
{
#if !(defined(Q_OS_MACOS) || defined(Q_OS_WIN))
    if (!ConfigHandler().showDesktopNotification()) {
        return;
    }
    auto bus = QDBusConnection::sessionBus();
    auto* connectionInterface = bus.interface();

    auto service = QStringLiteral("org.freedesktop.Notifications");
    auto path = QStringLiteral("/org/freedesktop/Notifications");
    auto interface = QStringLiteral("org.freedesktop.Notifications");

    if (connectionInterface->isServiceRegistered(service)) {
        m_interface = new QDBusInterface(service, path, interface, bus, this);
    } else {
        AbstractLogger::warning(AbstractLogger::Stderr |
                                AbstractLogger::LogFile)
          << tr("No DBus System Notification service found");
    }
#endif
}

void SystemNotification::sendMessage(const QString& text,
                                     const QString& savePath)
{
    sendMessage(text, tr("Flameshot Info"), savePath);
}

void SystemNotification::sendMessage(const QString& text,
                                     const QString& title,
                                     const QString& savePath,
                                     const int timeout)
{
    if (!ConfigHandler().showDesktopNotification()) {
        return;
    }

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    QMetaObject::invokeMethod(
      this,
      [&]() {
          // The call is queued to avoid recursive static initialization of
          // Flameshot and ConfigHandler.
          if (FlameshotDaemon::instance())
              FlameshotDaemon::instance()->sendTrayNotification(
                text, title, timeout);
      },
      Qt::QueuedConnection);
#else
    // A notification that points at a saved capture carries a button, and the
    // notification server reports the press back to the process that sent the
    // notification. `flameshot gui` quits as soon as the capture is saved, so
    // it hands the notification over to the daemon, which is still around to
    // open the folder when the button is finally pressed.
    if (!savePath.isEmpty() &&
        FlameshotDaemon::forwardNotification(text, title, savePath, timeout)) {
        return;
    }

    if (nullptr != m_interface && m_interface->isValid()) {
        QList<QVariant> args;
        QVariantMap hintsMap;
        QStringList actions;
        if (!savePath.isEmpty()) {
            QUrl fullPath = QUrl::fromLocalFile(savePath);
            // allows the notification to be dragged and dropped
            hintsMap[QStringLiteral("x-kde-urls")] =
              QStringList({ fullPath.toString() });
            if (FlameshotDaemon::instance() != nullptr) {
                // Only the daemon outlives its own notifications, so only the
                // daemon can offer a button. Pairs of (identifier, label).
                actions << OPEN_FOLDER_ACTION << tr("Open Folder");
            }
        }

        args << (qAppName())                 // appname
             << static_cast<unsigned int>(0) // id
             << FLAMESHOT_ICON               // icon
             << title                        // summary
             << text                         // body
             << actions                      // actions
             << hintsMap                     // hints
             << timeout;                     // timeout
        // Fire-and-forget: an asynchronous call never blocks the event loop,
        // even when no notification daemon is registered on the session bus
        // (e.g. bare startx / tiling WM sessions). The previous synchronous
        // callWithArgumentList stalled the main thread for the QtDBus reply
        // timeout (~25s) after every capture, freezing further captures until
        // it returned.
        auto pending = m_interface->asyncCallWithArgumentList(
          QStringLiteral("Notify"), args);

        if (!actions.isEmpty()) {
            // The server answers with the id it gave the notification, which is
            // how a button press is later matched to this capture. The watcher
            // belongs to the handler because this object is usually a temporary
            // that is gone before the answer arrives.
            auto* handler = NotificationActionHandler::instance();
            auto* watcher = new QDBusPendingCallWatcher(pending, handler);
            connect(watcher,
                    &QDBusPendingCallWatcher::finished,
                    handler,
                    [handler, savePath](QDBusPendingCallWatcher* self) {
                        QDBusPendingReply<uint> reply = *self;
                        if (!reply.isError()) {
                            handler->trackNotification(reply.value(), savePath);
                        }
                        self->deleteLater();
                    });
        }
    }
#endif
}
