#include "NetworkControl.h"
#include "Utilities.h"
#include <QDateTime>
#include <QDebug>

#undef signals
#include <gio/gio.h>

#define signals Q_SIGNALS

NetworkControl::NetworkControl(QObject *parent) : QObject(parent) {
    GError *error = nullptr;
    
    // Get the connection once and store it in the member variable
    m_conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);

    if (!m_conn) {
        qWarning("Failed to connect to system bus: %s", error->message);
        g_error_free(error);
    } 
}

NetworkControl::~NetworkControl() {
    if (m_conn) {
        g_object_unref(m_conn);
    }
}

QVariantMap NetworkControl::GetActiveDevice() {
    QVariantMap details;
    if (!m_conn) return details;

    GError *error = nullptr;

    // 1. Get all device paths
    GVariant *result = g_dbus_connection_call_sync(m_conn,
        "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager",
        "org.freedesktop.NetworkManager", "GetDevices",
        nullptr, G_VARIANT_TYPE("(ao)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

    if (!result) {
        if (error) { qWarning() << "GetDevices failed:" << error->message; g_error_free(error); }
        return details;
    }

    GVariantIter *iter;
    const gchar *path;
    g_variant_get(result, "(ao)", &iter);

    while (g_variant_iter_loop(iter, "o", &path)) {
        // 2. Check if this specific device is active (State 100)
        GVariant *vState = g_dbus_connection_call_sync(m_conn,
            "org.freedesktop.NetworkManager", path, "org.freedesktop.DBus.Properties",
            "Get", g_variant_new("(ss)", "org.freedesktop.NetworkManager.Device", "State"),
            G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);

        if (vState) {
            GVariant *inner;
            g_variant_get(vState, "(v)", &inner);
            guint32 state = g_variant_get_uint32(inner);
            g_variant_unref(inner);
            g_variant_unref(vState);

            if (state == 100) {
                // 3. Found the active device! Fetch ALL its properties.
                // Note: We use the base Device interface, but you could repeat 
                // this for .Wireless if you know it's a Wi-Fi device.
                GVariant *allProps = g_dbus_connection_call_sync(m_conn,
                    "org.freedesktop.NetworkManager", path, "org.freedesktop.DBus.Properties",
                    "GetAll", g_variant_new("(s)", "org.freedesktop.NetworkManager.Device"),
                    G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);

                if (allProps) {
                    GVariant *propDict;
                    g_variant_get(allProps, "(@a{sv})", &propDict); // Unwrap the tuple

                    GVariantIter pIter;
                    g_variant_iter_init(&pIter, propDict);
                    const gchar *key;
                    GVariant *val;

                    while (g_variant_iter_next(&pIter, "{sv}", &key, &val)) {
                        details.insert(QString::fromUtf8(key), gvariantToQVariant(val));
                        g_variant_unref(val);
                    }
                    
                    // Add the path itself as a convenience
                    details["DevicePath"] = QString::fromUtf8(path);

                    g_variant_unref(propDict);
                    g_variant_unref(allProps);
                }
                break; // Exit loop once active device is found
            }
        }
    }

    g_variant_iter_free(iter);
    g_variant_unref(result);
    return details;
}

QList<QVariantMap> NetworkControl::GetDevices() {
    QList<QVariantMap> devices;
    if (!m_conn) return devices;

    GError *error = nullptr;
    // Get the list of object paths for all devices
    GVariant *result = g_dbus_connection_call_sync(m_conn,
        "org.freedesktop.NetworkManager",        
        "/org/freedesktop/NetworkManager",
        "org.freedesktop.NetworkManager",
        "GetDevices",
        NULL,
        G_VARIANT_TYPE("(ao)"),
        G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    
    if (!result) {
        if (error) { qWarning() << "GetDevices failed:" << error->message; g_error_free(error); }
        return devices;
    }

    GVariantIter *iter;
    const gchar *devPath;
    g_variant_get(result, "(ao)", &iter);
    
    while (g_variant_iter_loop(iter, "o", &devPath)) {
        QVariantMap info;
        info["DevicePath"] = QString::fromUtf8(devPath);

        GVariant *props = g_dbus_connection_call_sync(m_conn,
            "org.freedesktop.NetworkManager",        
            devPath,
            "org.freedesktop.DBus.Properties",
            "GetAll",
            g_variant_new("(s)", "org.freedesktop.NetworkManager.Device"),
            G_VARIANT_TYPE("(a{sv})"),
            G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

        if (props) {
            GVariantIter *dictIter;
            g_variant_get(props, "(a{sv})", &dictIter);

            const gchar *key;
            GVariant *val;
            // Use 'sv' and 'iter_next' to ensure QML-compatible data ownership
            while (g_variant_iter_next(dictIter, "{sv}", &key, &val)) {                    
                info[QString::fromUtf8(key)] = gvariantToQVariant(val);
                g_variant_unref(val); 
            }
            g_variant_iter_free(dictIter);
            g_variant_unref(props);
        }
        devices.append(info);
    }
    g_variant_iter_free(iter);
    g_variant_unref(result);
    return devices;
}

QVariantMap NetworkControl::GetSettings(const QString &settingPath) {
    QVariantMap settings;
    if (!m_conn || settingPath.isEmpty()) return settings;

    GError *error = nullptr;
    GVariant *result = g_dbus_connection_call_sync(m_conn,
        "org.freedesktop.NetworkManager",
        settingPath.toUtf8().constData(),
        "org.freedesktop.NetworkManager.Settings.Connection",
        "GetSettings",
        nullptr,
        G_VARIANT_TYPE("(a{sa{sv}})"),
        G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    
    if (!result) {
        if (error) { qWarning() << "GetSettings failed:" << error->message; g_error_free(error); }
        return settings;
    }

    GVariantIter *outerIter;
    g_variant_get(result, "(a{sa{sv}})", &outerIter);

    const gchar *sectionName;
    GVariant *sectionDict;

    // Use next instead of loop for better control over the GVariant* dict
    while (g_variant_iter_next(outerIter, "{&s@a{sv}}", &sectionName, &sectionDict)) {
        QVariantMap sectionMap;
        GVariantIter innerIter;
        g_variant_iter_init(&innerIter, sectionDict);
        
        const gchar *key;
        GVariant *value;

        // CRITICAL: Use {sv} and iter_next to get a new reference for 'value'
        while (g_variant_iter_next(&innerIter, "{sv}", &key, &value)) {
            // gvariantToQVariant handles the 'v' unwrapping
            sectionMap.insert(QString::fromUtf8(key), gvariantToQVariant(value));
            g_variant_unref(value); // Must unref since iter_next ('v') increments ref count
        }

        settings.insert(QString::fromUtf8(sectionName), sectionMap);
        g_variant_unref(sectionDict); // Clean up the inner dictionary
    }

    g_variant_iter_free(outerIter);
    g_variant_unref(result);
    return settings;
}

void NetworkControl::RequestScan(const QString &devicePath) {
    GError *error = nullptr;
    qDebug() << "Request scan started";

    g_dbus_connection_call_sync(m_conn,
        "org.freedesktop.NetworkManager",
        devicePath.toUtf8().constData(),
        "org.freedesktop.NetworkManager.Device.Wireless",
        "RequestScan",
        g_variant_new("(a{sv})", NULL),
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        &error);
    
    if (error) {
        qWarning() << "RequestScan failed:" << error->message;
        g_error_free(error);
    }
}

QList<QVariantMap> NetworkControl::GetAccessPoints(const QString &devicePath) {
    QList<QVariantMap> aps;
    if (!m_conn || devicePath.isEmpty()) return aps;

    GError *error = nullptr;
    GVariant *result = g_dbus_connection_call_sync(m_conn,
        "org.freedesktop.NetworkManager",
        devicePath.toUtf8().constData(),
        "org.freedesktop.NetworkManager.Device.Wireless",
        "GetAccessPoints",
        nullptr,
        G_VARIANT_TYPE("(ao)"),
        G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    
    if (!result) {
        if (error) { qWarning() << "GetAccessPoints failed:" << error->message; g_error_free(error); }
        return aps;
    }

    GVariantIter *iter;
    const gchar *apPath;
    g_variant_get(result, "(ao)", &iter);
    
    while (g_variant_iter_loop(iter, "o", &apPath)) {
        QVariantMap info;
        QString pathStr = QString::fromUtf8(apPath);
        info["AccessPointPath"] = pathStr;

        // Optimization: Use the member connection and proper iterators
        GVariant *props = g_dbus_connection_call_sync(m_conn,
            "org.freedesktop.NetworkManager",        
            apPath,
            "org.freedesktop.DBus.Properties",
            "GetAll",
            g_variant_new("(s)", "org.freedesktop.NetworkManager.AccessPoint"),
            G_VARIANT_TYPE("(a{sv})"),
            G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

        if (props) {
            GVariantIter *dictIter;
            g_variant_get(props, "(a{sv})", &dictIter);

            const gchar *key;
            GVariant *val;
            // Use the 'sv' next pattern to prevent "undefined" in QML
            while (g_variant_iter_next(dictIter, "{sv}", &key, &val)) {
                info.insert(QString::fromUtf8(key), gvariantToQVariant(val));
                g_variant_unref(val); 
            }

            g_variant_iter_free(dictIter);
            g_variant_unref(props);
        }
        aps.append(info);
    }

    g_variant_iter_free(iter);
    g_variant_unref(result);
    return aps;
}

QList<QVariantMap> NetworkControl::GetKnownNetworksInRange(const QString &wifiDevicePath) {
    QList<QVariantMap> result;
    if (!m_conn || wifiDevicePath.isEmpty()) return result;

    // --- Step 1: Map SSIDs to Settings Paths AND Timestamps ---
    QMap<QString, QString> savedSsids;
    QMap<QString, quint64> savedTimestamps; // Store the 'timestamp' property

    GVariant *availRes = g_dbus_connection_call_sync(m_conn, "org.freedesktop.NetworkManager", 
        wifiDevicePath.toUtf8().constData(), "org.freedesktop.DBus.Properties", "Get", 
        g_variant_new("(ss)", "org.freedesktop.NetworkManager.Device", "AvailableConnections"), 
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);

    if (availRes) {
        GVariant *vList; 
        g_variant_get(availRes, "(v)", &vList);
        GVariantIter iter; 
        g_variant_iter_init(&iter, vList);
        const gchar *statePath;
        
        while (g_variant_iter_loop(&iter, "o", &statePath)) {
            GVariant *sSettingsRes = g_dbus_connection_call_sync(m_conn, "org.freedesktop.NetworkManager", statePath, 
                "org.freedesktop.NetworkManager.Settings.Connection", "GetSettings", nullptr, 
                G_VARIANT_TYPE("(a{sa{sv}})"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
            
            if (sSettingsRes) {
                GVariant *sDict;
                g_variant_get(sSettingsRes, "(@a{sa{sv}})", &sDict); 

                QString currentSsidName;
                quint64 lastTimestamp = 0;

                // A. Get SSID from '802-11-wireless'
                GVariant *wireless = g_variant_lookup_value(sDict, "802-11-wireless", G_VARIANT_TYPE("a{sv}"));
                if (wireless) {
                    GVariant *vSsid = g_variant_lookup_value(wireless, "ssid", G_VARIANT_TYPE("ay"));
                    if (vSsid) {
                        gsize len;
                        const guint8 *bytes = (const guint8*)g_variant_get_fixed_array(vSsid, &len, 1);
                        currentSsidName = QString::fromUtf8((const char*)bytes, len);
                        g_variant_unref(vSsid);
                    }
                    g_variant_unref(wireless);
                }

                // B. Get Timestamp from 'connection'
                GVariant *connection = g_variant_lookup_value(sDict, "connection", G_VARIANT_TYPE("a{sv}"));
                if (connection) {
                    GVariant *vTime = g_variant_lookup_value(connection, "timestamp", G_VARIANT_TYPE("t"));
                    if (vTime) {
                        lastTimestamp = g_variant_get_uint64(vTime);
                        g_variant_unref(vTime);
                    }
                    g_variant_unref(connection);
                }

                if (!currentSsidName.isEmpty()) {
                    savedSsids[currentSsidName] = QString::fromUtf8(statePath);
                    savedTimestamps[currentSsidName] = lastTimestamp;
                }

                g_variant_unref(sDict);
                g_variant_unref(sSettingsRes);
            }
        }
        g_variant_unref(vList); 
        g_variant_unref(availRes);
    }

    // --- Step 2: Get Access Points ---
    GVariant *apRes = g_dbus_connection_call_sync(m_conn, "org.freedesktop.NetworkManager", 
        wifiDevicePath.toUtf8().constData(), "org.freedesktop.NetworkManager.Device.Wireless", 
        "GetAccessPoints", nullptr, G_VARIANT_TYPE("(ao)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);

    if (apRes) {
        GVariantIter *apIter;
        const gchar *apPath;
        g_variant_get(apRes, "(ao)", &apIter);
        while (g_variant_iter_loop(apIter, "o", &apPath)) {
            GVariant *props = g_dbus_connection_call_sync(m_conn, 
                "org.freedesktop.NetworkManager", 
                apPath, 
                "org.freedesktop.DBus.Properties", 
                "GetAll", 
                g_variant_new("(s)", "org.freedesktop.NetworkManager.AccessPoint"),
                G_VARIANT_TYPE("(a{sv})"), 
                G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);

            if (props) {
                QVariantMap apMap;
                GVariantIter *pIter;
                g_variant_get(props, "(a{sv})", &pIter);
                
                const gchar *pKey;
                GVariant *pVal;
                QString currentSsid;
                
                while (g_variant_iter_next(pIter, "{sv}", &pKey, &pVal)) {
                    // DEBUG: Uncomment this to see every key NM sends for an AP
                    // qDebug() << "AP Property Key:" << pKey;

                    QVariant qv = gvariantToQVariant(pVal);
                    apMap[QString::fromUtf8(pKey)] = qv;
                  
                    if (g_strcmp0(pKey, "Ssid") == 0) {
                        currentSsid = qv.toString();
                    }
                    
                    g_variant_unref(pVal);
                }

                apMap["AccessPointPath"] = QString::fromUtf8(apPath);
                
                // Add Saved & LastConnected metadata
                if (!currentSsid.isEmpty() && savedSsids.contains(currentSsid)) {
                    apMap["Saved"] = true;
                    apMap["SettingsPath"] = savedSsids[currentSsid];
                    
                    // Logic for LastConnected: NM returns 0 if never connected
                    // We return the raw quint64 to QML
                    quint64 ts = savedTimestamps[currentSsid];
                    apMap["LastConnected"] = ts; 
                } else {
                    apMap["Saved"] = false;
                    apMap["SettingsPath"] = ""; 
                    apMap["LastConnected"] = 0;
                }

                result.append(apMap);
                g_variant_iter_free(pIter);
                g_variant_unref(props);
            }
        }
        g_variant_iter_free(apIter);
        g_variant_unref(apRes);
    }

    return result;
}

/**
 * Only works for known networks
 * For new networks, use AddAndActivateConnection
 * (If I ever got to adding that method)
 */
void NetworkControl::ActivateConnection(const QString &devicePath, const QString &settingsPath, const QString &accessPointPath) {
    // If any of these are empty, GDBus will throw a Critical Assertion
    if (devicePath.isEmpty() || settingsPath.isEmpty() || accessPointPath.isEmpty()) {
        qWarning() << "ActivateConnection aborted: Missing object paths.";
        return;
    }
    
    GError *error = nullptr;

    g_dbus_connection_call_sync(m_conn,
        "org.freedesktop.NetworkManager",        
        "/org/freedesktop/NetworkManager",
        "org.freedesktop.NetworkManager",
        "ActivateConnection",
        // The signature is (ooo): Connection, Device, SpecificObject
        g_variant_new("(ooo)", 
            settingsPath.toUtf8().constData(),
            devicePath.toUtf8().constData(),
            accessPointPath.toUtf8().constData()
        ),
        G_VARIANT_TYPE("(o)"), // This returns the path of the ActiveConnection object
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        &error);
    
    if (error) {
        qWarning() << "ActivateConnection failed:" << error->message;
        g_error_free(error);
    }
}

void NetworkControl::DisconnectDevice(const QString &devicePath) {
    if (!m_conn || devicePath.isEmpty()) return;

    GError *error = nullptr;

    // We call Disconnect on the Device interface. 
    // This works for both org.freedesktop.NetworkManager.Device.Wireless 
    // and org.freedesktop.NetworkManager.Device.Wired.

    // This tells the hardware to disconnect.
    // Crucially, it also prevents the device from auto-connecting to that same
    // network again until you manually intervene or reboot.

    g_dbus_connection_call_sync(m_conn,
        "org.freedesktop.NetworkManager",
        devicePath.toUtf8().constData(),
        "org.freedesktop.NetworkManager.Device",
        "Disconnect",
        nullptr, // No arguments required
        nullptr, // No return value expected
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        &error);

    if (error) {
        qWarning() << "Disconnect failed for" << devicePath << ":" << error->message;
        g_error_free(error);
    } else {
        qDebug() << "Disconnect command sent successfully to" << devicePath;
    }
}