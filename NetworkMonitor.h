#pragma once

#include <QObject>

class NetworkMonitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(int connectivityState READ connectivityState NOTIFY connectivityStateChanged)

public:
    explicit NetworkMonitor(QObject *parent = nullptr);
    int connectivityState() const { return m_connectivityState; }
    void setConnectivityState(int state);   // make public

signals:
    void connectivityStateChanged();

private:
    int m_connectivityState = 0;
};
