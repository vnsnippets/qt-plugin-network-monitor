#pragma once
#include <QObject>
#include <QVariantMap>

class NetworkMonitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(int connectivityState READ connectivityState NOTIFY connectivityStateChanged)
    Q_PROPERTY(int globalState READ globalState NOTIFY globalStateChanged)
    Q_PROPERTY(QVariantMap activeDevice READ activeDevice NOTIFY activeDeviceChanged)
    Q_DISABLE_COPY(NetworkMonitor) // Standard for singletons

    public:
        explicit NetworkMonitor(QObject *parent = nullptr);

        int connectivityState() const { return m_connectivityState; }
        int globalState() const { return m_globalState; }
        QVariantMap activeDevice() const { return m_activeDevice; }

        void SetConnectivityState(int state);
        void SetGlobalState(int state);
        void RefreshActiveDevice();

    signals:
        void activeDeviceChanged();
        void connectivityStateChanged();
        void globalStateChanged();
        void deviceAdded(const QString &path);
        void deviceRemoved(const QString &path);
        void scanFinished(const QString &devicePath);
        void deviceStateChanged(const QString &path, int state);

        // void activeConnectionStateChanged(const QString &path, int state);
        // void newConnection(const QString &path);
        // void connectionRemoved(const QString &path);

    private:
        int m_connectivityState = 0;
        int m_globalState = 0;
        QVariantMap m_activeDevice;
};
