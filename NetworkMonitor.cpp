#include "NetworkMonitor.h"
#include "Utilities.h"
#include <QDebug>

#undef signals
#include <gio/gio.h>

#define signals Q_SIGNALS

// --- Callbacks ---
static void on_nm_state_changed(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    auto monitor = static_cast<NetworkMonitor*>(user_data);
    guint32 new_state;
    g_variant_get(parameters, "(u)", &new_state);
    monitor->SetGlobalState(new_state);
}

// --- Callback: Wireless Scan Finished ---
static void on_wireless_props_changed(GDBusConnection*, const gchar*, const gchar* object_path, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    auto monitor = static_cast<NetworkMonitor*>(user_data);
    
    const gchar *iface;
    GVariant *changed_props;
    GVariant *invalidated;
    
    // Unpack the PropertiesChanged signal parameters
    g_variant_get(parameters, "(&s@a{sv}@as)", &iface, &changed_props, &invalidated);

    // NM emits this signal for several interfaces on the same object.
    // We only care about the Wireless interface.
    if (g_strcmp0(iface, "org.freedesktop.NetworkManager.Device.Wireless") == 0) {
        GVariantDict dict;
        g_variant_dict_init(&dict, changed_props);
        
        // We look for 'LastScanTime'. Note: NetworkManager only sends 
        // properties that ACTUALLY changed value.
        if (g_variant_dict_contains(&dict, "LastScanTime")) {
            qDebug() << "!!! Scan finished for device:" << object_path;
            emit monitor->scanFinished(QString::fromUtf8(object_path));
        }
    }
    
    g_variant_unref(changed_props);
    g_variant_unref(invalidated);
}

// --- Callback: Device State Changed ---
static void on_device_state_changed(GDBusConnection*, const gchar*, const gchar* object_path, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    auto monitor = static_cast<NetworkMonitor*>(user_data);
    guint32 new_state, old_state, reason;
    g_variant_get(parameters, "(uuu)", &new_state, &old_state, &reason);
    emit monitor->deviceStateChanged(QString::fromUtf8(object_path), (int)new_state);
    
    // Refresh the kept active device whenever ANY device changes state
    monitor->RefreshActiveDevice();
}

// --- Callback: Global NM Properties (Connectivity) ---
static void on_nm_properties_changed(GDBusConnection* conn, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    auto monitor = static_cast<NetworkMonitor*>(user_data);
    const gchar *iface;
    GVariant *changed;
    GVariant *invalidated;
    g_variant_get(parameters, "(&s@a{sv}@as)", &iface, &changed, &invalidated);

    if (g_strcmp0(iface, "org.freedesktop.NetworkManager") == 0) {
        // 1. Check Connectivity
        GVariant *vConn = g_variant_lookup_value(changed, "Connectivity", nullptr);
        if (vConn) {
            monitor->SetConnectivityState(g_variant_get_uint32(vConn));
            g_variant_unref(vConn);
        }

        // 2. Check for ActiveConnection changes
        // If ActiveConnections or PrimaryConnection changes, update our kept device
        if (g_variant_dict_contains(g_variant_dict_new(changed), "ActiveConnections") ||
            g_variant_dict_contains(g_variant_dict_new(changed), "PrimaryConnection")) {
            monitor->RefreshActiveDevice();
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

static void on_ap_props_changed(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    auto monitor = static_cast<NetworkMonitor*>(user_data);
    
    GVariant *changed_props;
    // Parameters: (InterfaceName, ChangedProperties, InvalidatedProperties)
    g_variant_get(parameters, "(&s@a{sv}@as)", nullptr, &changed_props, nullptr);

    GVariantDict dict;
    g_variant_dict_init(&dict, changed_props);
    
    // 3. Check if "Strength" is actually in this specific update
    if (g_variant_dict_contains(&dict, "Strength")) {
        GVariant *vStrength = g_variant_dict_lookup_value(&dict, "Strength", G_VARIANT_TYPE_BYTE);
        if (vStrength) {
            int newStrength = (int)g_variant_get_byte(vStrength);
            
            // 4. Update the QVariantMap safely on the main thread
            QMetaObject::invokeMethod(monitor, [monitor, newStrength]() {
                QVariantMap ap = monitor->activeAccessPoint();
                ap["Strength"] = newStrength;
                monitor->SetActiveAccessPoint(ap); 
            }, Qt::QueuedConnection);

            g_variant_unref(vStrength);
        }
    }
    g_variant_unref(changed_props);
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

    // 1. Watch for Connectivity & Global State
    g_dbus_connection_signal_subscribe(conn, "org.freedesktop.NetworkManager", "org.freedesktop.NetworkManager", "StateChanged",
        "/org/freedesktop/NetworkManager", nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_nm_state_changed, this, nullptr);

    g_dbus_connection_signal_subscribe(conn, "org.freedesktop.NetworkManager", "org.freedesktop.DBus.Properties", "PropertiesChanged", 
        "/org/freedesktop/NetworkManager", nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_nm_properties_changed, this, nullptr);

    // 2. Watch for Device Add/Remove
    g_dbus_connection_signal_subscribe(conn, "org.freedesktop.NetworkManager", "org.freedesktop.NetworkManager", "DeviceAdded", 
        "/org/freedesktop/NetworkManager", nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_device_added, this, nullptr);
    
    g_dbus_connection_signal_subscribe(conn, "org.freedesktop.NetworkManager", "org.freedesktop.NetworkManager", "DeviceRemoved",
        "/org/freedesktop/NetworkManager", nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_device_removed, this, nullptr);

    // 3. Watch for Wireless Scan Results (LastScanTime update)
    g_dbus_connection_signal_subscribe(conn, "org.freedesktop.NetworkManager", "org.freedesktop.DBus.Properties", "PropertiesChanged",
        nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_wireless_props_changed, this, nullptr);

    // 4. Watch for Device State (Connecting, Disconnected, etc.)
    g_dbus_connection_signal_subscribe(conn, "org.freedesktop.NetworkManager", "org.freedesktop.NetworkManager.Device", "StateChanged", 
        nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_device_state_changed, this, nullptr);

    // Initial Fetch for Connectivity
    GVariant *res = g_dbus_connection_call_sync(conn, "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager", "org.freedesktop.DBus.Properties",
        "Get", g_variant_new("(ss)", "org.freedesktop.NetworkManager", "Connectivity"), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
    
        if (res) {
        GVariant *inner; g_variant_get(res, "(v)", &inner);
        SetConnectivityState(g_variant_get_uint32(inner));
        g_variant_unref(inner); g_variant_unref(res);
    }

    // Initial Fetch for Global State
    GVariant *resState = g_dbus_connection_call_sync(conn, "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager", "org.freedesktop.DBus.Properties",
        "Get", g_variant_new("(ss)", "org.freedesktop.NetworkManager", "State"), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
    if (resState) {
        GVariant *inner; g_variant_get(resState, "(v)", &inner);
        SetGlobalState(g_variant_get_uint32(inner));
        g_variant_unref(inner); g_variant_unref(resState);
    }
    
    RefreshActiveDevice();
}

// --- Setters ---
void NetworkMonitor::SetConnectivityState(int state) {
    if (m_connectivityState != state) {
        m_connectivityState = state;
        qDebug() << "Connectivity updated:" << state;
        emit connectivityStateChanged();
    }
}

void NetworkMonitor::SetGlobalState(int state) {
    if (m_globalState != state) {
        m_globalState = state;
        qDebug() << "Global NM state updated:" << state;
        emit globalStateChanged();
    }
}

void NetworkMonitor::RefreshActiveDevice() {
    GError *error = nullptr;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (!conn) return;

    QVariantMap newDevice;

    // 1. Get the 'PrimaryConnection' path from NetworkManager
    GVariant *vPrimary = g_dbus_connection_call_sync(conn,
        "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager",
        "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", "org.freedesktop.NetworkManager", "PrimaryConnection"),
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);

    if (vPrimary) {
        GVariant *vPath;
        g_variant_get(vPrimary, "(v)", &vPath);
        const gchar *activeConnPath = g_variant_get_string(vPath, nullptr);

        // If path is "/" or empty, no active connection exists
        if (g_strcmp0(activeConnPath, "/") != 0) {
            
            // 2. Get the Device(s) associated with this Active Connection
            GVariant *vDevices = g_dbus_connection_call_sync(conn,
                "org.freedesktop.NetworkManager", activeConnPath,
                "org.freedesktop.DBus.Properties", "Get",
                g_variant_new("(ss)", "org.freedesktop.NetworkManager.Connection.Active", "Devices"),
                G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);

            if (vDevices) {
                GVariant *vInner;
                g_variant_get(vDevices, "(v)", &vInner);
                GVariantIter *iter;
                g_variant_get(vInner, "ao", &iter);
                const gchar *devicePath;

                if (g_variant_iter_loop(iter, "o", &devicePath)) {
                    // 3. Fetch all properties for this specific device
                    GVariant *allProps = g_dbus_connection_call_sync(conn,
                        "org.freedesktop.NetworkManager", devicePath,
                        "org.freedesktop.DBus.Properties", "GetAll",
                        g_variant_new("(s)", "org.freedesktop.NetworkManager.Device"),
                        G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);

                    if (allProps) {
                        GVariant *propDict;
                        g_variant_get(allProps, "(@a{sv})", &propDict);
                        
                        GVariantIter pIter;
                        g_variant_iter_init(&pIter, propDict);
                        const gchar *key;
                        GVariant *val;

                        while (g_variant_iter_next(&pIter, "{sv}", &key, &val)) {
                            // Using your utility to convert
                            newDevice.insert(QString::fromUtf8(key), gvariantToQVariant(val));
                            g_variant_unref(val);
                        }
                        
                        newDevice["DevicePath"] = QString::fromUtf8(devicePath);

                        // Device Type 2: WiFi | Device Type 30: P2P WiFi
                        if (newDevice["DeviceType"].toInt() == 2) { // 2 = Wireless
                            RefreshActiveAccessPoint(newDevice["DevicePath"].toString());
                        } else {
                            // Clear AP data if we switched to Ethernet
                            m_activeAccessPoint.clear();
                            emit activeAccessPointChanged();
                        }

                        g_variant_unref(propDict);
                        g_variant_unref(allProps);
                    }
                }
                g_variant_iter_free(iter);
                g_variant_unref(vInner);
                g_variant_unref(vDevices);
            }
        }
        g_variant_unref(vPath);
        g_variant_unref(vPrimary);
    }

    // Only notify QML if the data actually changed to save CPU
    if (m_activeDevice != newDevice) {
        m_activeDevice = newDevice;
        emit activeDeviceChanged();
        qDebug() << "Active device updated to:" << m_activeDevice["Interface"].toString();
    }
    
    g_object_unref(conn); // Cleanup temporary connection
}

void NetworkMonitor::RefreshActiveAccessPoint(const QString &devicePath) {
    GError *error = nullptr;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (!conn) return;

    // 1. Get the 'ActiveAccessPoint' property from the Wireless Device
    GVariant *vAp = g_dbus_connection_call_sync(conn,
        "org.freedesktop.NetworkManager", devicePath.toUtf8().constData(),
        "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", "org.freedesktop.NetworkManager.Device.Wireless", "ActiveAccessPoint"),
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);

    if (vAp) {
        GVariant *vPath;
        g_variant_get(vAp, "(v)", &vPath);
        QString apPath = QString::fromUtf8(g_variant_get_string(vPath, nullptr));

        if (apPath != "/" && !apPath.isEmpty()) {
            // 2. Get all AP properties (Ssid, Strength, Frequency, etc.)
            GVariant *allProps = g_dbus_connection_call_sync(conn,
                "org.freedesktop.NetworkManager", apPath.toUtf8().constData(),
                "org.freedesktop.DBus.Properties", "GetAll",
                g_variant_new("(s)", "org.freedesktop.NetworkManager.AccessPoint"),
                G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);

            if (allProps) {
                GVariant *propDict;
                g_variant_get(allProps, "(@a{sv})", &propDict);
                
                QVariantMap apDetails;
                GVariantIter pIter;
                g_variant_iter_init(&pIter, propDict);
                const gchar *key;
                GVariant *val;

                while (g_variant_iter_next(&pIter, "{sv}", &key, &val)) {
                    apDetails[QString::fromUtf8(key)] = gvariantToQVariant(val);
                    g_variant_unref(val);
                }

                apDetails["Path"] = apPath; // Keep the path for monitoring
                
                // Thread safety: Update the member variable
                m_activeAccessPoint = apDetails;
                emit activeAccessPointChanged();
                
                SubscribeToAccessPointStrength(apPath);
                g_variant_unref(propDict);
                g_variant_unref(allProps);
            }
        }
        g_variant_unref(vPath);
        g_variant_unref(vAp);
    }
    g_object_unref(conn);
}

void NetworkMonitor::SubscribeToAccessPointStrength(const QString &apPath) {
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    
    // 1. Clean up previous subscription to prevent memory leaks/multiple triggers
    if (m_apSubscriptionId > 0) {
        g_dbus_connection_signal_unsubscribe(conn, m_apSubscriptionId);
        m_apSubscriptionId = 0;
    }

    // 2. Only subscribe if we have a valid AP path (not "/" or empty)
    if (apPath != "/" && !apPath.isEmpty()) {
        m_apSubscriptionId = g_dbus_connection_signal_subscribe(conn,
            "org.freedesktop.NetworkManager",            // Sender
            "org.freedesktop.DBus.Properties",           // Interface
            "PropertiesChanged",                         // Member
            apPath.toUtf8().constData(),                 // SPECIFIC Object Path
            "org.freedesktop.NetworkManager.AccessPoint", // arg0 (Interface name)
            G_DBUS_SIGNAL_FLAGS_NONE,
            on_ap_props_changed,                         // Callback function
            this,                                        // User data
            nullptr);
    }
    g_object_unref(conn);
}

void NetworkMonitor::SetActiveAccessPoint(const QVariantMap &ap) {
    if (m_activeAccessPoint != ap) {
        m_activeAccessPoint = ap;
        emit activeAccessPointChanged();
        // Optional: qDebug() << "Active AP Strength updated:" << ap["Strength"].toInt();
    }
}