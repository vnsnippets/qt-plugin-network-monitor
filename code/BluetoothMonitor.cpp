#include "BluetoothMonitor.h"
#include "DBusFactory.h"
#include <QDebug>
#include <QPointer>
#include <QCoreApplication>

#undef signals
#include <gio/gio.h>
#define signals Q_SIGNALS

static void on_bt_properties_changed(GDBusConnection*, const gchar*, const gchar*, const gchar* iface, const gchar*, GVariant* parameters, gpointer user_data) {
    BluetoothMonitor* monitorRaw = static_cast<BluetoothMonitor*>(user_data);
    if (!monitorRaw) return;

    const gchar *changed_iface;
    GVariant *changed_props;
    GVariant *invalidated;
    g_variant_get(parameters, "(&s@a{sv}@as)", &changed_iface, &changed_props, &invalidated);

    if (g_strcmp0(changed_iface, "org.bluez.Adapter1") == 0) {
        GVariant *vPowered = g_variant_lookup_value(changed_props, "Powered", G_VARIANT_TYPE_BOOLEAN);
        if (vPowered) {
            bool powered = g_variant_get_boolean(vPowered);
            QPointer<BluetoothMonitor> q_monitor(monitorRaw);
            QMetaObject::invokeMethod(q_monitor, [q_monitor, powered]() {
                if (q_monitor) {
                    q_monitor->SetBluetoothEnabled(powered);
                }
            }, Qt::QueuedConnection);
            g_variant_unref(vPowered);
        }
    }
    g_variant_unref(changed_props);
    g_variant_unref(invalidated);
}

BluetoothMonitor::BluetoothMonitor(QObject *parent) : QObject(parent) {
    m_dbusConn = DBusFactory::connection();
    if (!m_dbusConn) {
        qWarning("BluetoothMonitor: Failed to get shared DBus connection.");
        return;
    }

    // Watch for Bluetooth property changes
    m_btPropsSubId = g_dbus_connection_signal_subscribe(m_dbusConn, "org.bluez", "org.freedesktop.DBus.Properties", "PropertiesChanged",
        nullptr, "org.bluez.Adapter1", G_DBUS_SIGNAL_FLAGS_NONE, on_bt_properties_changed, this, nullptr);

    RefreshBluetoothStatus();
}

BluetoothMonitor::~BluetoothMonitor() {
    if (m_dbusConn && m_btPropsSubId > 0) {
        g_dbus_connection_signal_unsubscribe(m_dbusConn, m_btPropsSubId);
        m_btPropsSubId = 0;
    }
}

void BluetoothMonitor::SetBluetoothEnabled(bool enabled) {
    if (m_bluetoothEnabled != enabled) {
        m_bluetoothEnabled = enabled;
        qDebug() << "Bluetooth status updated:" << enabled;
        emit bluetoothEnabledChanged();
    }
}

void BluetoothMonitor::RefreshBluetoothStatus() {
    if (!m_dbusConn) return;

    GError *error = nullptr;
    GVariant *res = g_dbus_connection_call_sync(m_dbusConn, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects", nullptr, G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

    if (res) {
        GVariantIter *objects;
        g_variant_get(res, "(a{oa{sa{sv}}})", &objects);
        
        bool anyPowered = false;
        const gchar *path;
        GVariant *ifaces;
        
        while (g_variant_iter_loop(objects, "{o@a{sa{sv}}}", &path, &ifaces)) {
            GVariant *adapter = g_variant_lookup_value(ifaces, "org.bluez.Adapter1", G_VARIANT_TYPE("a{sv}"));
            if (adapter) {
                GVariant *vPowered = g_variant_lookup_value(adapter, "Powered", G_VARIANT_TYPE_BOOLEAN);
                if (vPowered) {
                    if (g_variant_get_boolean(vPowered)) {
                        anyPowered = true;
                    }
                    g_variant_unref(vPowered);
                }
                g_variant_unref(adapter);
            }
            if (anyPowered) break;
        }
        g_variant_iter_free(objects);
        g_variant_unref(res);
        
        SetBluetoothEnabled(anyPowered);
    } else {
        if (error) {
            qWarning() << "BluetoothMonitor: Failed to fetch managed objects:" << error->message;
            g_error_free(error);
        }
        SetBluetoothEnabled(false);
    }
}
