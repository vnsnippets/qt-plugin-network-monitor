#include "NetworkMonitor.h"
#include "DBusFactory.h"
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
    
    // Safely move to main thread
    QMetaObject::invokeMethod(monitor, [monitor, new_state]() {
        monitor->SetGlobalState(new_state);
    }, Qt::QueuedConnection);
}

// --- Callback: Device State Changed ---
static void on_device_state_changed(GDBusConnection*, const gchar*, const gchar* object_path, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    auto monitor = static_cast<NetworkMonitor*>(user_data);
    guint32 new_state, old_state, reason;
    g_variant_get(parameters, "(uuu)", &new_state, &old_state, &reason);
    QString path = QString::fromUtf8(object_path);

    QMetaObject::invokeMethod(monitor, [monitor, path, new_state]() {
        emit monitor->deviceStateChanged(path, (int)new_state);
        monitor->RefreshActiveDevice();
    }, Qt::QueuedConnection);
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
            if (g_variant_dict_contains(&dict, "LastScanTime")) {
                QString path = QString::fromUtf8(object_path);
                QMetaObject::invokeMethod(monitor, [monitor, path]() {
                    emit monitor->scanFinished(path);
                }, Qt::QueuedConnection);
            }
        }
    }
    
    g_variant_unref(changed_props);
    g_variant_unref(invalidated);
}

// --- Callback: Global NM Properties (Connectivity) ---
static void on_nm_properties_changed(GDBusConnection* m_dbusConn, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    auto monitor = static_cast<NetworkMonitor*>(user_data);
    const gchar *iface;
    GVariant *changed;
    GVariant *invalidated;
    g_variant_get(parameters, "(&s@a{sv}@as)", &iface, &changed, &invalidated);

    if (g_strcmp0(iface, "org.freedesktop.NetworkManager") == 0) {
        // Check Connectivity
        GVariant *vConn = g_variant_lookup_value(changed, "Connectivity", G_VARIANT_TYPE_UINT32);
        if (vConn) {
            // Extract values first
            int connectivity = g_variant_get_uint32(vConn);

            // Capture the value (connectivity), not the GVariant pointer
            QMetaObject::invokeMethod(monitor, [monitor, connectivity]() {
                monitor->SetConnectivityState(connectivity);
            }, Qt::QueuedConnection);
            g_variant_unref(vConn);
        }

        // Only refresh device if the connections list actually changed
        if (g_variant_lookup_value(changed, "ActiveConnections", nullptr) || 
            g_variant_lookup_value(changed, "PrimaryConnection", nullptr)) {
            QMetaObject::invokeMethod(monitor, [monitor]() {
                monitor->RefreshActiveDevice();
            }, Qt::QueuedConnection);
        }
    }
    g_variant_unref(changed);
    g_variant_unref(invalidated);
}

static void on_device_added(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    auto monitor = static_cast<NetworkMonitor*>(user_data);
    const gchar *path_raw;
    g_variant_get(parameters, "(o)", &path_raw);
    QString path = QString::fromUtf8(path_raw); // Copy the string immediately

    QMetaObject::invokeMethod(monitor, [monitor, path]() {
        emit monitor->deviceAdded(path);
    }, Qt::QueuedConnection);
}

static void on_device_removed(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    auto monitor = static_cast<NetworkMonitor*>(user_data);
    const gchar *path_raw;
    g_variant_get(parameters, "(o)", &path_raw);
    QString path = QString::fromUtf8(path_raw); // Copy the string immediately

    QMetaObject::invokeMethod(monitor, [monitor, path]() {
        emit monitor->deviceRemoved(path);
    }, Qt::QueuedConnection);
}

static void on_ap_props_changed(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    auto monitor = static_cast<NetworkMonitor*>(user_data);
    
    GVariant *changed_props;
    g_variant_get(parameters, "(&s@a{sv}@as)", nullptr, &changed_props, nullptr);

    GVariantDict dict;
    g_variant_dict_init(&dict, changed_props);
    
    if (g_variant_dict_contains(&dict, "Strength")) {
        GVariant *vStrength = g_variant_dict_lookup_value(&dict, "Strength", G_VARIANT_TYPE_BYTE);
        if (vStrength) {
            // 1. Extract the value to a local variable IMMEDIATELY
            int newStrength = (int)g_variant_get_byte(vStrength);
            g_variant_unref(vStrength);

            // 2. Pass the 'int', NOT the 'GVariant*', into the lambda
            QMetaObject::invokeMethod(monitor, [monitor, newStrength]() {
                QVariantMap ap = monitor->activeAccessPoint();
                ap["Strength"] = newStrength;
                monitor->SetActiveAccessPoint(ap); 
            }, Qt::QueuedConnection);
        }
    }
    g_variant_unref(changed_props);
}

// Similar callbacks can be added for ActiveConnection and Settings signals…

// --- Constructor ---
NetworkMonitor::NetworkMonitor(QObject *parent) : QObject(parent) {
    // Get the shared connection
    m_dbusConn = DBusFactory::connection();
    if (!m_dbusConn) {
        qWarning("NetworkMonitor: Failed to get shared DBus connection.");
    }

    // 1. Watch for Connectivity & Global State
    m_stateSubId = g_dbus_connection_signal_subscribe(m_dbusConn, "org.freedesktop.NetworkManager", "org.freedesktop.NetworkManager", "StateChanged",
        "/org/freedesktop/NetworkManager", nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_nm_state_changed, this, nullptr);

    m_propsSubId = g_dbus_connection_signal_subscribe(m_dbusConn, "org.freedesktop.NetworkManager", "org.freedesktop.DBus.Properties", "PropertiesChanged", 
        "/org/freedesktop/NetworkManager", nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_nm_properties_changed, this, nullptr);

    // 2. Watch for Device Add/Remove
    m_deviceAddedSubId = g_dbus_connection_signal_subscribe(m_dbusConn, "org.freedesktop.NetworkManager", "org.freedesktop.NetworkManager", "DeviceAdded", 
        "/org/freedesktop/NetworkManager", nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_device_added, this, nullptr);
    
    m_deviceRemovedSubId = g_dbus_connection_signal_subscribe(m_dbusConn, "org.freedesktop.NetworkManager", "org.freedesktop.NetworkManager", "DeviceRemoved",
        "/org/freedesktop/NetworkManager", nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_device_removed, this, nullptr);

    // 3. Watch for Wireless Scan Results (LastScanTime update)
    m_wirelessPropsSubId = g_dbus_connection_signal_subscribe(m_dbusConn, "org.freedesktop.NetworkManager", "org.freedesktop.DBus.Properties", "PropertiesChanged",
        nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_wireless_props_changed, this, nullptr);

    // 4. Watch for Device State (Connecting, Disconnected, etc.)
    m_deviceStateSubId = g_dbus_connection_signal_subscribe(m_dbusConn, "org.freedesktop.NetworkManager", "org.freedesktop.NetworkManager.Device", "StateChanged", 
        nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_device_state_changed, this, nullptr);

    // Initial Fetch for Connectivity
    GVariant *res = g_dbus_connection_call_sync(m_dbusConn, "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager", "org.freedesktop.DBus.Properties",
        "Get", g_variant_new("(ss)", "org.freedesktop.NetworkManager", "Connectivity"), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
    
        if (res) {
        GVariant *inner; g_variant_get(res, "(v)", &inner);
        SetConnectivityState(g_variant_get_uint32(inner));
        g_variant_unref(inner); g_variant_unref(res);
    }

    // Initial Fetch for Global State
    GVariant *resState = g_dbus_connection_call_sync(m_dbusConn, "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager", "org.freedesktop.DBus.Properties",
        "Get", g_variant_new("(ss)", "org.freedesktop.NetworkManager", "State"), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
    if (resState) {
        GVariant *inner; g_variant_get(resState, "(v)", &inner);
        SetGlobalState(g_variant_get_uint32(inner));
        g_variant_unref(inner); g_variant_unref(resState);
    }
    
    RefreshActiveDevice();
}

NetworkMonitor::~NetworkMonitor() {
    if (m_dbusConn) {
        // Safe unsubscription using the IDs you tracked
        if (m_stateSubId > 0) g_dbus_connection_signal_unsubscribe(m_dbusConn, m_stateSubId);
        if (m_propsSubId > 0) g_dbus_connection_signal_unsubscribe(m_dbusConn, m_propsSubId);
        if (m_deviceAddedSubId > 0) g_dbus_connection_signal_unsubscribe(m_dbusConn, m_deviceAddedSubId);
        if (m_deviceRemovedSubId > 0) g_dbus_connection_signal_unsubscribe(m_dbusConn, m_deviceRemovedSubId);
        if (m_wirelessPropsSubId > 0) g_dbus_connection_signal_unsubscribe(m_dbusConn, m_wirelessPropsSubId);
        if (m_deviceStateSubId > 0) g_dbus_connection_signal_unsubscribe(m_dbusConn, m_deviceStateSubId);
        if (m_apSubscriptionId > 0) g_dbus_connection_signal_unsubscribe(m_dbusConn, m_apSubscriptionId);

        m_dbusConn = nullptr; // Safety: Prevent any late callbacks from using it
    }
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
    if (!m_dbusConn) return;

    GVariant *vPrimary = g_dbus_connection_call_sync(m_dbusConn,
        "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager",
        "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", "org.freedesktop.NetworkManager", "PrimaryConnection"),
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
    
    if (!vPrimary) return;

    GVariant *vInnerPath;
    g_variant_get(vPrimary, "(v)", &vInnerPath);
    const gchar *activeConnPath = g_variant_get_string(vInnerPath, nullptr);

    // FIX: Check if the connection path is actually different from what we already have
    // before doing all the extra work.
    if (m_activeDevice["_nm_connection_path"].toString() == QString::fromUtf8(activeConnPath)) {
        g_variant_unref(vInnerPath);
        g_variant_unref(vPrimary);
        return; 
    }

    QVariantMap newDevice;
    if (g_strcmp0(activeConnPath, "/") != 0) {
        GVariant *vDevices = g_dbus_connection_call_sync(m_dbusConn,
            "org.freedesktop.NetworkManager", activeConnPath,
            "org.freedesktop.DBus.Properties", "Get",
            g_variant_new("(ss)", "org.freedesktop.NetworkManager.Connection.Active", "Devices"),
            G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);

        if (vDevices) {
            GVariant *vInnerArray;
            g_variant_get(vDevices, "(v)", &vInnerArray);
            GVariantIter *iter;
            g_variant_get(vInnerArray, "ao", &iter);
            const gchar *devicePath;

            if (g_variant_iter_loop(iter, "o", &devicePath)) {
                GVariant *allProps = g_dbus_connection_call_sync(m_dbusConn,
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
                        newDevice.insert(QString::fromUtf8(key), gvariantToQVariant(val));
                        g_variant_unref(val);
                    }
                    newDevice["DevicePath"] = QString::fromUtf8(devicePath);
                    if (newDevice["DeviceType"].toInt() == 2) { 
                        RefreshActiveAccessPoint(newDevice["DevicePath"].toString());
                    }
                    g_variant_unref(propDict);
                    g_variant_unref(allProps);
                }
            }
            g_variant_iter_free(iter);
            g_variant_unref(vInnerArray);
            g_variant_unref(vDevices);
        }
    }

    g_variant_unref(vInnerPath);
    g_variant_unref(vPrimary);

    if (m_activeDevice != newDevice) {
        m_activeDevice = newDevice;
        emit activeDeviceChanged();
    }

    newDevice["_nm_connection_path"] = QString::fromUtf8(activeConnPath);
}

void NetworkMonitor::RefreshActiveAccessPoint(const QString &devicePath) {
    if (!m_dbusConn) return;

    // 1. Get the 'ActiveAccessPoint' property from the Wireless Device
    GVariant *vAp = g_dbus_connection_call_sync(m_dbusConn,
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
            GVariant *allProps = g_dbus_connection_call_sync(m_dbusConn,
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
}

void NetworkMonitor::SubscribeToAccessPointStrength(const QString &apPath) {
    if (!m_dbusConn) return;

    // Optional: Check if we are already subscribed to this exact path to avoid churn
    static QString lastPath;
    if (apPath == lastPath && m_apSubscriptionId > 0) return;
    lastPath = apPath;

    if (m_apSubscriptionId > 0) {
        g_dbus_connection_signal_unsubscribe(m_dbusConn, m_apSubscriptionId);
        m_apSubscriptionId = 0;
    }

    // Only subscribe if we have a valid AP path (not "/" or empty)
    if (apPath != "/" && !apPath.isEmpty()) {
        m_apSubscriptionId = g_dbus_connection_signal_subscribe(m_dbusConn,
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
}

void NetworkMonitor::SetActiveAccessPoint(const QVariantMap &ap) {
    if (m_activeAccessPoint != ap) {
        m_activeAccessPoint = ap;
        emit activeAccessPointChanged();
        // Optional: qDebug() << "Active AP Strength updated:" << ap["Strength"].toInt();
    }
}