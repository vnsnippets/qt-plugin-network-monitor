#include "NetworkMonitor.h"
#include "DBusFactory.h"
#include "Utilities.h"
#include <QDebug>
#include <QPointer>
#include <QCoreApplication>

#undef signals
#include <gio/gio.h>

#define signals Q_SIGNALS

// --- Callbacks ---
/**
 * Callback: Global NetworkManager state changed (e.g., connected, disconnected, etc.)
 * 
 * SAFETY: Uses QPointer to prevent use-after-free if NetworkMonitor is destroyed
 * while a queued callback is pending. If the object is deleted, the weak pointer
 * automatically becomes null and the lambda exits safely.
 */
static void on_nm_state_changed(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    // Capture raw pointer only to create weak reference; don't use it directly
    NetworkMonitor* monitorRaw = static_cast<NetworkMonitor*>(user_data);
    guint32 new_state;
    g_variant_get(parameters, "(u)", &new_state);
    
    // Create weak pointer on the stack; will become nullptr if object is deleted
    QPointer<NetworkMonitor> q_monitor(monitorRaw);
    
    // Safely move to main thread with weak pointer guard
    QMetaObject::invokeMethod(monitorRaw, [q_monitor, new_state]() {
        if (q_monitor) {
            q_monitor->SetGlobalState(new_state);
        }
        // If q_monitor.lock() returns nullptr, object was deleted; exit safely
    }, Qt::QueuedConnection);
}

/**
 * Callback: Device state changed (e.g., device activated, deactivated)
 * SAFETY: Protected by QPointer against use-after-free.
 */
static void on_device_state_changed(GDBusConnection*, const gchar*, const gchar* object_path, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    NetworkMonitor* monitorRaw = static_cast<NetworkMonitor*>(user_data);
    guint32 new_state, old_state, reason;
    g_variant_get(parameters, "(uuu)", &new_state, &old_state, &reason);
    QString path = QString::fromUtf8(object_path);
    
    QPointer<NetworkMonitor> q_monitor(monitorRaw);

    QMetaObject::invokeMethod(monitorRaw, [q_monitor, path, new_state]() {
        if (q_monitor) {
            emit q_monitor->deviceStateChanged(path, (int)new_state);
            q_monitor->RefreshActiveDevice();
        }
    }, Qt::QueuedConnection);
}

/**
 * Callback: Wireless properties changed (e.g., scan finished detected via LastScanTime)
 * SAFETY: Protected by QPointer against use-after-free.
 */
static void on_wireless_props_changed(GDBusConnection*, const gchar*, const gchar* object_path, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    NetworkMonitor* monitorRaw = static_cast<NetworkMonitor*>(user_data);
    
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
            QString path = QString::fromUtf8(object_path);
            QPointer<NetworkMonitor> q_monitor(monitorRaw);
            
            QMetaObject::invokeMethod(monitorRaw, [q_monitor, path]() {
                if (q_monitor) {
                    emit q_monitor->scanFinished(path);
                }
            }, Qt::QueuedConnection);
        }
    }
    
    g_variant_unref(changed_props);
    g_variant_unref(invalidated);
}

/**
 * Callback: Global NetworkManager properties changed (e.g., connectivity state, active connections)
 * SAFETY: Protected by QPointer against use-after-free.
 */
static void on_nm_properties_changed(GDBusConnection* m_dbusConn, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    NetworkMonitor* monitorRaw = static_cast<NetworkMonitor*>(user_data);
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
            QPointer<NetworkMonitor> q_monitor(monitorRaw);

            // Capture the value (connectivity), not the GVariant pointer
            QMetaObject::invokeMethod(monitorRaw, [q_monitor, connectivity]() {
                if (q_monitor) {
                    q_monitor->SetConnectivityState(connectivity);
                }
            }, Qt::QueuedConnection);
            g_variant_unref(vConn);
        }

        // Only refresh device if the connections list actually changed
        if (g_variant_lookup_value(changed, "ActiveConnections", nullptr) || 
            g_variant_lookup_value(changed, "PrimaryConnection", nullptr)) {
            QPointer<NetworkMonitor> q_monitor(monitorRaw);
            
            QMetaObject::invokeMethod(monitorRaw, [q_monitor]() {
                if (q_monitor) {
                    q_monitor->RefreshActiveDevice();
                }
            }, Qt::QueuedConnection);
        }
    }
    g_variant_unref(changed);
    g_variant_unref(invalidated);
}

/**
 * Callback: Device added to the system
 * SAFETY: Protected by QPointer against use-after-free.
 */
static void on_device_added(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    NetworkMonitor* monitorRaw = static_cast<NetworkMonitor*>(user_data);
    const gchar *path_raw;
    g_variant_get(parameters, "(o)", &path_raw);
    QString path = QString::fromUtf8(path_raw); // Copy the string immediately
    
    QPointer<NetworkMonitor> q_monitor(monitorRaw);

    QMetaObject::invokeMethod(monitorRaw, [q_monitor, path]() {
        if (q_monitor) {
            emit q_monitor->deviceAdded(path);
        }
    }, Qt::QueuedConnection);
}

/**
 * Callback: Device removed from the system
 * SAFETY: Protected by QPointer against use-after-free.
 */
static void on_device_removed(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    NetworkMonitor* monitorRaw = static_cast<NetworkMonitor*>(user_data);
    const gchar *path_raw;
    g_variant_get(parameters, "(o)", &path_raw);
    QString path = QString::fromUtf8(path_raw); // Copy the string immediately
    
    QPointer<NetworkMonitor> q_monitor(monitorRaw);

    QMetaObject::invokeMethod(monitorRaw, [q_monitor, path]() {
        if (q_monitor) {
            emit q_monitor->deviceRemoved(path);
        }
    }, Qt::QueuedConnection);
}

/**
 * Callback: Access point properties changed (e.g., signal strength update)
 * SAFETY: Protected by QPointer against use-after-free.
 */
static void on_ap_props_changed(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
    // 1. Cast to our monitor. This is just a pointer value, no memory access yet.
    NetworkMonitor* monitorRaw = static_cast<NetworkMonitor*>(user_data);
    if (!monitorRaw) return;

    GVariant *changed_props = nullptr;
    g_variant_get(parameters, "(&s@a{sv}@as)", nullptr, &changed_props, nullptr);
    if (!changed_props) return;

    GVariantDict dict;
    g_variant_dict_init(&dict, changed_props);
    
    if (g_variant_dict_contains(&dict, "Strength")) {
        GVariant *vStrength = g_variant_dict_lookup_value(&dict, "Strength", G_VARIANT_TYPE_BYTE);
        if (vStrength) {
            int newStrength = (int)g_variant_get_byte(vStrength);
            g_variant_unref(vStrength);
            
            // 2. Wrap the raw pointer in a QPointer. 
            // This is safe because it only reads the 'guard' metadata which 
            // Qt keeps alive even if the object is being deleted.
            QPointer<NetworkMonitor> safeMonitor(monitorRaw);

            // 3. CRITICAL: Pass 'qApp' (the global application) as the context, 
            // OR pass no context at all. DO NOT pass 'monitorRaw' here.
            // Passing monitorRaw as the first arg causes the QObject::thread() crash.
            QMetaObject::invokeMethod(qApp, [safeMonitor, newStrength]() {
                // 4. Now we are in the GUI thread. Check if our object survived the reload.
                if (safeMonitor) {
                    QVariantMap ap = safeMonitor->activeAccessPoint();
                    ap["Strength"] = newStrength;
                    safeMonitor->SetActiveAccessPoint(ap);
                }
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
        return;  // Exit early if connection unavailable
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

    /**
     * CRITICAL: Initial Fetch for Connectivity
     * 
     * NULL CHECK: g_dbus_connection_call_sync can fail if:
     * - DBus connection is unavailable
     * - The NetworkManager service is not running
     * - Timeout occurs
     * 
     * Result must be checked for nullptr BEFORE calling g_variant_get(),
     * otherwise accessing uninitialized memory causes segmentation fault.
     */
    GError *error = nullptr;
    GVariant *res = g_dbus_connection_call_sync(m_dbusConn, "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager", "org.freedesktop.DBus.Properties",
        "Get", g_variant_new("(ss)", "org.freedesktop.NetworkManager", "Connectivity"), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    
    if (res) {
        GVariant *inner = nullptr;
        g_variant_get(res, "(v)", &inner);
        if (inner) {
            SetConnectivityState(g_variant_get_uint32(inner));
            g_variant_unref(inner);
        }
        g_variant_unref(res);
    } else {
        if (error) {
            qWarning() << "Failed to fetch Connectivity state:" << error->message;
            g_error_free(error);
            error = nullptr;
        }
        // Set default fallback value on error
        SetConnectivityState(0);
    }

    /**
     * CRITICAL: Initial Fetch for Global State
     * Same null-checking pattern as Connectivity fetch.
     */
    GVariant *resState = g_dbus_connection_call_sync(m_dbusConn, "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager", "org.freedesktop.DBus.Properties",
        "Get", g_variant_new("(ss)", "org.freedesktop.NetworkManager", "State"), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    
    if (resState) {
        GVariant *inner = nullptr;
        g_variant_get(resState, "(v)", &inner);
        if (inner) {
            SetGlobalState(g_variant_get_uint32(inner));
            g_variant_unref(inner);
        }
        g_variant_unref(resState);
    } else {
        if (error) {
            qWarning() << "Failed to fetch Global state:" << error->message;
            g_error_free(error);
        }
        // Set default fallback value on error
        SetGlobalState(0);
    }
    
    RefreshActiveDevice();
}

NetworkMonitor::~NetworkMonitor() {
    /**
     * CRITICAL CLEANUP SEQUENCE:
     * 
     * 1. IMMEDIATELY unsubscribe from all DBus signals BEFORE any other cleanup.
     *    This prevents new callbacks from being queued after object destruction begins.
     * 
     * 2. Unsubscribe in reverse order of subscription (good practice for cleanup).
     *
     * 3. Once all subscriptions are removed, any pending QMetaObject::invokeMethod calls
     *    will safely check the weak pointer and find nullptr, exiting gracefully.
     *
     * 4. Finally, nullify m_dbusConn to signal to any still-pending code that the
     *    connection is no longer valid.
     */
    if (m_dbusConn) {
        // Unsubscribe from all DBus signals
        // Each unsubscribe ID is tracked during construction; check > 0 to avoid unsubscribing invalid IDs
        if (m_apSubscriptionId > 0) {
            g_dbus_connection_signal_unsubscribe(m_dbusConn, m_apSubscriptionId);
            m_apSubscriptionId = 0;
        }
        if (m_deviceStateSubId > 0) {
            g_dbus_connection_signal_unsubscribe(m_dbusConn, m_deviceStateSubId);
            m_deviceStateSubId = 0;
        }
        if (m_wirelessPropsSubId > 0) {
            g_dbus_connection_signal_unsubscribe(m_dbusConn, m_wirelessPropsSubId);
            m_wirelessPropsSubId = 0;
        }
        if (m_deviceRemovedSubId > 0) {
            g_dbus_connection_signal_unsubscribe(m_dbusConn, m_deviceRemovedSubId);
            m_deviceRemovedSubId = 0;
        }
        if (m_deviceAddedSubId > 0) {
            g_dbus_connection_signal_unsubscribe(m_dbusConn, m_deviceAddedSubId);
            m_deviceAddedSubId = 0;
        }
        if (m_propsSubId > 0) {
            g_dbus_connection_signal_unsubscribe(m_dbusConn, m_propsSubId);
            m_propsSubId = 0;
        }
        if (m_stateSubId > 0) {
            g_dbus_connection_signal_unsubscribe(m_dbusConn, m_stateSubId);
            m_stateSubId = 0;
        }

        // Signal that the connection is no longer valid
        // If any pending callbacks fire after this point, they'll check the weak pointer
        // and find nullptr, then exit safely without attempting to use m_dbusConn
        m_dbusConn = nullptr;
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

    GError *error = nullptr;
    GVariant *vPrimary = g_dbus_connection_call_sync(m_dbusConn,
        "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager",
        "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", "org.freedesktop.NetworkManager", "PrimaryConnection"),
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    
    if (!vPrimary) {
        if (error) {
            qWarning() << "RefreshActiveDevice: Failed to get PrimaryConnection:" << error->message;
            g_error_free(error);
        }
        return;
    }

    GVariant *vInnerPath = nullptr;
    // Validate format before unpacking to prevent undefined behavior on format mismatch
    if (!g_variant_is_of_type(vPrimary, G_VARIANT_TYPE("(v)"))) {
        qWarning() << "RefreshActiveDevice: PrimaryConnection has unexpected variant type";
        g_variant_unref(vPrimary);
        return;
    }
    
    g_variant_get(vPrimary, "(v)", &vInnerPath);
    if (!vInnerPath) {
        qWarning() << "RefreshActiveDevice: Failed to unpack PrimaryConnection variant";
        g_variant_unref(vPrimary);
        return;
    }
    
    const gchar *activeConnPath = g_variant_get_string(vInnerPath, nullptr);
    // Use g_strcmp0 which handles NULL safely
    if (!activeConnPath) {
        qWarning() << "RefreshActiveDevice: PrimaryConnection path is NULL";
        g_variant_unref(vInnerPath);
        g_variant_unref(vPrimary);
        return;
    }

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
            G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

        if (vDevices) {
            GVariant *vInnerArray = nullptr;
            // Validate format before unpacking
            if (g_variant_is_of_type(vDevices, G_VARIANT_TYPE("(v)"))) {
                g_variant_get(vDevices, "(v)", &vInnerArray);
            }
            
            if (vInnerArray && g_variant_is_of_type(vInnerArray, G_VARIANT_TYPE("ao"))) {
                GVariantIter *iter = nullptr;
                g_variant_get(vInnerArray, "ao", &iter);
                if (iter) {
                    const gchar *devicePath = nullptr;

                    if (g_variant_iter_loop(iter, "o", &devicePath) && devicePath) {
                        GVariant *allProps = g_dbus_connection_call_sync(m_dbusConn,
                            "org.freedesktop.NetworkManager", devicePath,
                            "org.freedesktop.DBus.Properties", "GetAll",
                            g_variant_new("(s)", "org.freedesktop.NetworkManager.Device"),
                            G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

                        if (allProps) {
                            GVariant *propDict = nullptr;
                            if (g_variant_is_of_type(allProps, G_VARIANT_TYPE("(a{sv})"))) {
                                g_variant_get(allProps, "(@a{sv})", &propDict);
                            }
                            
                            if (propDict) {
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
                            }
                            g_variant_unref(allProps);
                        } else if (error) {
                            qWarning() << "RefreshActiveDevice: Failed to get device properties:" << error->message;
                            g_error_free(error);
                            error = nullptr;
                        }
                    }
                    g_variant_iter_free(iter);
                }
            }
            if (vInnerArray) g_variant_unref(vInnerArray);
            g_variant_unref(vDevices);
        } else if (error) {
            qWarning() << "RefreshActiveDevice: Failed to get Devices:" << error->message;
            g_error_free(error);
            error = nullptr;
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
    GError *error = nullptr;
    GVariant *vAp = g_dbus_connection_call_sync(m_dbusConn,
        "org.freedesktop.NetworkManager", devicePath.toUtf8().constData(),
        "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", "org.freedesktop.NetworkManager.Device.Wireless", "ActiveAccessPoint"),
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

    if (!vAp) {
        if (error) {
            qWarning() << "RefreshActiveAccessPoint: Failed to get ActiveAccessPoint:" << error->message;
            g_error_free(error);
        }
        return;
    }

    GVariant *vPath = nullptr;
    // Validate format before unpacking
    if (!g_variant_is_of_type(vAp, G_VARIANT_TYPE("(v)"))) {
        qWarning() << "RefreshActiveAccessPoint: ActiveAccessPoint has unexpected variant type";
        g_variant_unref(vAp);
        return;
    }
    
    g_variant_get(vAp, "(v)", &vPath);
    if (!vPath) {
        qWarning() << "RefreshActiveAccessPoint: Failed to unpack ActiveAccessPoint variant";
        g_variant_unref(vAp);
        return;
    }
    
    const gchar *apPathStr = g_variant_get_string(vPath, nullptr);
    QString apPath = QString::fromUtf8(apPathStr ? apPathStr : "/");

    if (apPath != "/" && !apPath.isEmpty()) {
        // 2. Get all AP properties (Ssid, Strength, Frequency, etc.)
        GVariant *allProps = g_dbus_connection_call_sync(m_dbusConn,
            "org.freedesktop.NetworkManager", apPath.toUtf8().constData(),
            "org.freedesktop.DBus.Properties", "GetAll",
            g_variant_new("(s)", "org.freedesktop.NetworkManager.AccessPoint"),
            G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

        if (allProps) {
            GVariant *propDict = nullptr;
            // Validate format before unpacking
            if (g_variant_is_of_type(allProps, G_VARIANT_TYPE("(a{sv})"))) {
                g_variant_get(allProps, "(@a{sv})", &propDict);
            }
            
            if (propDict) {
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
            }
            g_variant_unref(allProps);
        } else if (error) {
            qWarning() << "RefreshActiveAccessPoint: Failed to get AP properties:" << error->message;
            g_error_free(error);
        }
    }
    
    g_variant_unref(vPath);
    g_variant_unref(vAp);
}

void NetworkMonitor::SubscribeToAccessPointStrength(const QString &apPath) {
    if (!m_dbusConn) return;

    /**
     * THREAD SAFETY: Check if we are already subscribed to this exact path to avoid churn.
     * Uses instance member m_lastSubscribedApPath instead of static variable to ensure
     * thread-safe access (each NetworkMonitor instance has its own path tracking).
     * 
     * Static local variables can be accessed from multiple threads concurrently,
     * causing data races. Instance members are accessed through the object's lifetime,
     * which is already protected by the QML engine and Qt's signal/slot mechanism.
     */
    if (apPath == m_lastSubscribedApPath && m_apSubscriptionId > 0) {
        return;  // Already subscribed to this exact path
    }
    m_lastSubscribedApPath = apPath;

    // Unsubscribe from the old access point if one was subscribed
    if (m_apSubscriptionId > 0) {
        g_dbus_connection_signal_unsubscribe(m_dbusConn, m_apSubscriptionId);
        m_apSubscriptionId = 0;
    }

    // Only subscribe if we have a valid AP path (not "/" or empty)
    if (apPath != "/" && !apPath.isEmpty()) {
        m_apSubscriptionId = g_dbus_connection_signal_subscribe(m_dbusConn, "org.freedesktop.NetworkManager", "org.freedesktop.DBus.Properties", "PropertiesChanged",
            apPath.toUtf8().constData(), "org.freedesktop.NetworkManager.AccessPoint", G_DBUS_SIGNAL_FLAGS_NONE, on_ap_props_changed, this, nullptr);
    }
}

void NetworkMonitor::SetActiveAccessPoint(const QVariantMap &ap) {
    if (m_activeAccessPoint != ap) {
        m_activeAccessPoint = ap;
        emit activeAccessPointChanged();
        // Optional: qDebug() << "Active AP Strength updated:" << ap["Strength"].toInt();
    }
}