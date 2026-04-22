#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QList>

struct DeviceInfo {
    QString path;
    int type;
    QString interface;
};

typedef struct _GDBusConnection GDBusConnection;

class NetworkControl : public QObject {
    Q_OBJECT
    public:
        explicit NetworkControl(QObject *parent = nullptr);
        ~NetworkControl();

        Q_INVOKABLE QList<QVariantMap> GetDevices();
        Q_INVOKABLE QVariantMap GetSettings(const QString &settingsPath);
        Q_INVOKABLE void RequestScan(const QString &devicePath);
        Q_INVOKABLE QList<QVariantMap> GetAccessPoints(const QString &devicePath);
        Q_INVOKABLE QList<QVariantMap> GetKnownNetworksInRange();
        Q_INVOKABLE void ActivateConnection(
            const QString &devicePath,
            const QString &connectionPath,
            const QString &specificObjectPath
        );
    private:
        GDBusConnection *m_conn = nullptr; // Member variable to hold the connection
};
