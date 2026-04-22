#pragma once
#include <QObject>

class NetworkMonitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(int connectivityState READ connectivityState NOTIFY connectivityStateChanged)
    Q_PROPERTY(int globalState READ globalState NOTIFY globalStateChanged)

    public:
        explicit NetworkMonitor(QObject *parent = nullptr);

        int connectivityState() const { return m_connectivityState; }
        int globalState() const { return m_globalState; }

        void setConnectivityState(int state);
        void setGlobalState(int state);

    signals:
        void connectivityStateChanged();
        void globalStateChanged();
        void deviceAdded(const QString &path);
        void deviceRemoved(const QString &path);
        void deviceStateChanged(const QString &path, int state);
        void activeConnectionStateChanged(const QString &path, int state);
        void newConnection(const QString &path);
        void connectionRemoved(const QString &path);

    private:
        int m_connectivityState = 0;
        int m_globalState = 0;
};
