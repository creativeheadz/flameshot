// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QHash>
#include <QObject>

class QDBusInterface;

/**
 * @brief Reacts to the buttons of the desktop notifications we send.
 *
 * The notification server reports a button press back to the process that sent
 * the notification, which may happen long after the SystemNotification that
 * sent it is gone, so this handler lives for as long as the application does.
 */
class NotificationActionHandler : public QObject
{
    Q_OBJECT
public:
    static NotificationActionHandler* instance();

    /// Remember the capture a notification refers to, so its button can act.
    void trackNotification(uint notificationId, const QString& savePath);

private slots:
    void onActionInvoked(uint notificationId, const QString& actionKey);
    void onNotificationClosed(uint notificationId, uint reason);

private:
    explicit NotificationActionHandler(QObject* parent = nullptr);

    QHash<uint, QString> m_savePaths;
};

class SystemNotification : public QObject
{
    Q_OBJECT
public:
    explicit SystemNotification(QObject* parent = nullptr);

    void sendMessage(const QString& text, const QString& savePath = {});

    void sendMessage(const QString& text,
                     const QString& title,
                     const QString& savePath,
                     const int timeout = 5000);

private:
    QDBusInterface* m_interface;
};
