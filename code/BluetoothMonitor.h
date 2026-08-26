#pragma once
#include <QObject>
#include <QtQml/qqmlregistration.h>

typedef struct _GDBusConnection GDBusConnection;

class BluetoothMonitor : public QObject {
    Q_OBJECT QML_ELEMENT QML_SINGLETON
    Q_PROPERTY(bool bluetoothEnabled READ bluetoothEnabled NOTIFY bluetoothEnabledChanged)
    Q_DISABLE_COPY(BluetoothMonitor)

    public:
        explicit BluetoothMonitor(QObject *parent = nullptr);
        ~BluetoothMonitor();

        bool bluetoothEnabled() const { return m_bluetoothEnabled; }
        void SetBluetoothEnabled(bool enabled);

    signals:
        void bluetoothEnabledChanged();

    private:
        GDBusConnection *m_dbusConn = nullptr;
        bool m_bluetoothEnabled = false;
        unsigned int m_btPropsSubId = 0;

        void RefreshBluetoothStatus();
};
