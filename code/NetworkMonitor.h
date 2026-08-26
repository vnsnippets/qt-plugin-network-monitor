#pragma once
#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

typedef struct _GDBusConnection GDBusConnection;

class NetworkMonitor : public QObject {
    Q_OBJECT QML_ELEMENT QML_SINGLETON
    Q_PROPERTY(int ConnectivityState READ connectivityState NOTIFY connectivityStateChanged)
    Q_PROPERTY(int GlobalState READ globalState NOTIFY globalStateChanged)
    Q_PROPERTY(QVariantMap ActiveDevice READ activeDevice NOTIFY activeDeviceChanged)
    Q_PROPERTY(QVariantMap ActiveAccessPoint READ activeAccessPoint NOTIFY activeAccessPointChanged)
    Q_PROPERTY(bool wirelessEnabled READ wirelessEnabled NOTIFY wirelessEnabledChanged)
    Q_PROPERTY(bool wirelessHardwareEnabled READ wirelessHardwareEnabled NOTIFY wirelessHardwareEnabledChanged)
    Q_PROPERTY(bool wwanEnabled READ wwanEnabled NOTIFY wwanEnabledChanged)
    Q_PROPERTY(bool wwanHardwareEnabled READ wwanHardwareEnabled NOTIFY wwanHardwareEnabledChanged)
    Q_DISABLE_COPY(NetworkMonitor) // Standard for singletons

    public:
        explicit NetworkMonitor(QObject *parent = nullptr);
        ~NetworkMonitor();

        int connectivityState() const { return m_connectivityState; }
        int globalState() const { return m_globalState; }
        QVariantMap activeDevice() const { return m_activeDevice; }
        QVariantMap activeAccessPoint() const { return m_activeAccessPoint; }
        bool wirelessEnabled() const { return m_wirelessEnabled; }
        bool wirelessHardwareEnabled() const { return m_wirelessHardwareEnabled; }
        bool wwanEnabled() const { return m_wwanEnabled; }
        bool wwanHardwareEnabled() const { return m_wwanHardwareEnabled; }

        void SetConnectivityState(int state);
        void SetGlobalState(int state);
        void SetActiveAccessPoint(const QVariantMap &ap);
        void SetWirelessEnabled(bool enabled);
        void SetWirelessHardwareEnabled(bool enabled);
        void SetWwanEnabled(bool enabled);
        void SetWwanHardwareEnabled(bool enabled);
        Q_INVOKABLE void RefreshActiveDevice();

    signals:
        void activeDeviceChanged();
        void connectivityStateChanged();
        void globalStateChanged();
        void deviceAdded(const QString &path);
        void deviceRemoved(const QString &path);
        void scanFinished(const QString &devicePath);
        void deviceStateChanged(const QString &path, int state);
        void activeAccessPointChanged();
        void wirelessEnabledChanged();
        void wirelessHardwareEnabledChanged();
        void wwanEnabledChanged();
        void wwanHardwareEnabledChanged();

        // void activeConnectionStateChanged(const QString &path, int state);
        // void newConnection(const QString &path);
        // void connectionRemoved(const QString &path);

    private:
        // Store IDs for every subscription
        unsigned int m_stateSubId = 0;
        unsigned int m_propsSubId = 0;
        unsigned int m_deviceAddedSubId = 0;
        unsigned int m_deviceRemovedSubId = 0;
        unsigned int m_wirelessPropsSubId = 0;
        unsigned int m_deviceStateSubId = 0;
        unsigned int m_apSubscriptionId = 0; 

        GDBusConnection *m_dbusConn = nullptr;
        int m_connectivityState = 0;
        int m_globalState = 0;
        QVariantMap m_activeDevice;
        QVariantMap m_activeAccessPoint;

        bool m_wirelessEnabled = false;
        bool m_wirelessHardwareEnabled = false;
        bool m_wwanEnabled = false;
        bool m_wwanHardwareEnabled = false;
        
        /// Instance member to track last subscribed AP path (replaces static variable for thread safety)
        QString m_lastSubscribedApPath;
        
        void SubscribeToAccessPointStrength(const QString &apPath);
        void RefreshActiveAccessPoint(const QString &devicePath);
};
