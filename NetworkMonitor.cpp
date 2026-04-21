#include <gio/gio.h>   // GLib first
#include "NetworkMonitor.h"
#include <QDebug>

// Avoid macro clash
#undef signals

static void on_nm_state_changed(GDBusConnection *conn,
                                const gchar *,
                                const gchar *,
                                const gchar *,
                                const gchar *,
                                GVariant *parameters,
                                gpointer user_data)
{
    auto monitor = static_cast<NetworkMonitor*>(user_data);
    guint32 new_state;
    g_variant_get(parameters, "(u)", &new_state);
    monitor->setConnectivityState(new_state);
}

NetworkMonitor::NetworkMonitor(QObject *parent)
    : QObject(parent)
{
    GError *error = nullptr;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (!conn) {
        qWarning("Failed to connect to system bus: %s", error->message);
        g_error_free(error);
        return;
    }

    g_dbus_connection_signal_subscribe(conn,
        "org.freedesktop.NetworkManager",
        "org.freedesktop.NetworkManager",
        "StateChanged",
        "/org/freedesktop/NetworkManager",
        nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_nm_state_changed,
        this,
        nullptr);

    // Fetch initial Connectivity property
    GVariant *result = g_dbus_connection_call_sync(
        conn,
        "org.freedesktop.NetworkManager",
        "/org/freedesktop/NetworkManager",
        "org.freedesktop.DBus.Properties",
        "Get",
        g_variant_new("(ss)", "org.freedesktop.NetworkManager", "Connectivity"),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        &error);

    if (result) {
        GVariant *val;
        g_variant_get(result, "(v)", &val);
        guint32 connectivity = g_variant_get_uint32(val);
        setConnectivityState(connectivity);
        g_variant_unref(val);
        g_variant_unref(result);
    }
}

void NetworkMonitor::setConnectivityState(int state) {
    if (m_connectivityState != state) {
        m_connectivityState = state;
        qDebug() << "Connectivity state updated:" << state;
        emit connectivityStateChanged();
    }
}