#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QList>
#include <QtQml/qqmlregistration.h>

struct DeviceInfo {
    QString path;
    int type;
    QString interface;
};

typedef struct _GDBusConnection GDBusConnection;

class NetworkControl : public QObject {
    Q_OBJECT QML_ELEMENT QML_SINGLETON
    Q_DISABLE_COPY(NetworkControl) // Standard for singletons

    public:
        explicit NetworkControl(QObject *parent = nullptr);
        ~NetworkControl();

        Q_INVOKABLE QVariantMap GetActiveDevice();
        Q_INVOKABLE QList<QVariantMap> GetAllDevices();
        Q_INVOKABLE QVariantMap GetSettings(const QString &settingsPath);
        Q_INVOKABLE void RequestScan(const QString &devicePath);
        Q_INVOKABLE QList<QVariantMap> GetAccessPoints(const QString &devicePath);
        Q_INVOKABLE QList<QVariantMap> GetKnownNetworksInRange(const QString &devicePath);
        Q_INVOKABLE void ActivateConnection(const QString &devicePath, const QString &settingsPath, const QString &accessPointPath);
        Q_INVOKABLE void DisconnectDevice(const QString &devicePath);


        Q_INVOKABLE QVariantMap GetDeviceProperties(const QString &devicePath);
    private:
        GDBusConnection *m_dbusConn = nullptr;
};
