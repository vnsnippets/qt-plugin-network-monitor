// #include <gio/gio.h>
#include "NetworkMonitor.h"
#include <QDebug>

#undef signals
#include <gio/gio.h>

#define signals Q_SIGNALS

// --- Callbacks ---
static void on_nm_state_changed(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    auto monitor = static_cast<NetworkMonitor*>(user_data);
    guint32 new_state;
    g_variant_get(parameters, "(u)", &new_state);
    monitor->setGlobalState(new_state);
}

static void on_nm_properties_changed(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    auto monitor = static_cast<NetworkMonitor*>(user_data);
    const gchar *iface;
    GVariant *changed;
    GVariant *invalidated;
    g_variant_get(parameters, "(&s@a{sv}@as)", &iface, &changed, &invalidated);

    if (g_strcmp0(iface, "org.freedesktop.NetworkManager") == 0) {
        GVariant *val = g_variant_lookup_value(changed, "Connectivity", NULL);
        if (val) {
            guint32 connectivity = g_variant_get_uint32(val);
            monitor->setConnectivityState(connectivity);
            g_variant_unref(val);
        }
    }
    g_variant_unref(changed);
    g_variant_unref(invalidated);
}

static void on_device_added(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    auto monitor = static_cast<NetworkMonitor*>(user_data);
    const gchar *path;
    g_variant_get(parameters, "(o)", &path);
    emit monitor->deviceAdded(QString::fromUtf8(path));
}

static void on_device_removed(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    auto monitor = static_cast<NetworkMonitor*>(user_data);
    const gchar *path;
    g_variant_get(parameters, "(o)", &path);
    emit monitor->deviceRemoved(QString::fromUtf8(path));
}

// Similar callbacks can be added for ActiveConnection and Settings signals…

// --- Constructor ---
NetworkMonitor::NetworkMonitor(QObject *parent) : QObject(parent) {
    GError *error = nullptr;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (!conn) {
        qWarning("Failed to connect to system bus: %s", error->message);
        g_error_free(error);
        return;
    }

    // Global signals
    g_dbus_connection_signal_subscribe(conn,
        "org.freedesktop.NetworkManager",
        "org.freedesktop.NetworkManager",
        "StateChanged",
        "/org/freedesktop/NetworkManager",
        nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        on_nm_state_changed, this, nullptr);

    g_dbus_connection_signal_subscribe(conn,
        "org.freedesktop.NetworkManager",
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        "/org/freedesktop/NetworkManager",
        nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        on_nm_properties_changed, this, nullptr);

    g_dbus_connection_signal_subscribe(conn,
        "org.freedesktop.NetworkManager",
        "org.freedesktop.NetworkManager",
        "DeviceAdded",
        "/org/freedesktop/NetworkManager",
        nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        on_device_added, this, nullptr);

    g_dbus_connection_signal_subscribe(conn,
        "org.freedesktop.NetworkManager",
        "org.freedesktop.NetworkManager",
        "DeviceRemoved",
        "/org/freedesktop/NetworkManager",
        nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        on_device_removed, this, nullptr);

    // TODO: Add subscriptions for Device.StateChanged, ActiveConnection.StateChanged, Settings.NewConnection, etc.

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

// --- Setters ---
void NetworkMonitor::setConnectivityState(int state) {
    if (m_connectivityState != state) {
        m_connectivityState = state;
        qDebug() << "Connectivity updated:" << state;
        emit connectivityStateChanged();
    }
}

void NetworkMonitor::setGlobalState(int state) {
    if (m_globalState != state) {
        m_globalState = state;
        qDebug() << "Global NM state updated:" << state;
        emit globalStateChanged();
    }
}
