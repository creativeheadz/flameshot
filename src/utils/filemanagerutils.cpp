// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "filemanagerutils.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
#include <QProcess>
#else
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#endif

namespace {

void openContainingFolder(const QString& path)
{
    const QString folder = QFileInfo(path).absolutePath();
    if (!folder.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    }
}

} // namespace

void FileManagerUtils::revealFile(const QString& path)
{
    if (path.isEmpty()) {
        return;
    }
    const QString file = QFileInfo(path).absoluteFilePath();
    if (!QFileInfo::exists(file)) {
        // The capture was moved or deleted in the meantime.
        openContainingFolder(file);
        return;
    }

#if defined(Q_OS_WIN)
    // No space is allowed between '/select,' and the path.
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            { QStringLiteral("/select,") +
                              QDir::toNativeSeparators(file) });
#elif defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"),
                            { QStringLiteral("-R"), file });
#else
    // org.freedesktop.FileManager1 is the desktop-agnostic way of asking for a
    // file to be revealed. It is implemented by Nautilus, Dolphin, Nemo,
    // Thunar, PCManFM, Caja... and is D-Bus activatable, so the file manager
    // does not need to be running already.
    QDBusMessage message = QDBusMessage::createMethodCall(
      QStringLiteral("org.freedesktop.FileManager1"),
      QStringLiteral("/org/freedesktop/FileManager1"),
      QStringLiteral("org.freedesktop.FileManager1"),
      QStringLiteral("ShowItems"));
    message << QStringList{ QUrl::fromLocalFile(file).toString() }
            << QString(); // startup id

    // Asynchronous: starting a file manager can take a while, and this may be
    // called from the daemon, whose event loop must keep running.
    auto* watcher = new QDBusPendingCallWatcher(
      QDBusConnection::sessionBus().asyncCall(message), qApp);
    QObject::connect(watcher,
                     &QDBusPendingCallWatcher::finished,
                     qApp,
                     [file](QDBusPendingCallWatcher* self) {
                         if (self->isError()) {
                             // No file manager implements the interface.
                             openContainingFolder(file);
                         }
                         self->deleteLater();
                     });
#endif
}
