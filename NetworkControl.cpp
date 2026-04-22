#include "NetworkControl.h"
#include "Utilities.h"
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
    if (!m_conn) return result;

    GError *error = nullptr;

    if (wifiDevicePath.isEmpty()) return result;

    // --- Step 2: Map SSIDs to Settings Paths ---
    QMap<QString, QString> savedSsids;
    GVariant *availRes = g_dbus_connection_call_sync(m_conn, "org.freedesktop.NetworkManager", wifiDevicePath.toUtf8().constData(),
        "org.freedesktop.DBus.Properties", "Get", g_variant_new("(ss)", "org.freedesktop.NetworkManager.Device", "AvailableConnections"), 
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);

    if (availRes) {
        GVariant *vList; 
        g_variant_get(availRes, "(v)", &vList); // vList is now the 'ao' (array of object paths)
        
        GVariantIter iter; 
        g_variant_iter_init(&iter, vList);
        const gchar *satePath;
        
        while (g_variant_iter_loop(&iter, "o", &satePath)) {
            GVariant *sSettingsRes = g_dbus_connection_call_sync(m_conn, "org.freedesktop.NetworkManager", satePath, 
                "org.freedesktop.NetworkManager.Settings.Connection", "GetSettings", nullptr, G_VARIANT_TYPE("(a{sa{sv}})"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
            
            if (sSettingsRes) {
                GVariant *sDict;
                // CRITICAL FIX: Extract the dictionary from the return tuple '(a{sa{sv}})'
                g_variant_get(sSettingsRes, "(@a{sa{sv}})", &sDict); 

                // Now sDict is a valid dictionary ('a{sa{sv}}') for lookup
                GVariant *wireless = g_variant_lookup_value(sDict, "802-11-wireless", G_VARIANT_TYPE("a{sv}"));
                if (wireless) {
                    GVariant *vSsid = g_variant_lookup_value(wireless, "ssid", G_VARIANT_TYPE("ay"));
                    if (vSsid) {
                        gsize len;
                        const guint8 *bytes = (const guint8*)g_variant_get_fixed_array(vSsid, &len, 1);
                        if (bytes) {
                            savedSsids[QString::fromUtf8((const char*)bytes, len)] = QString::fromUtf8(satePath);
                        }
                        g_variant_unref(vSsid);
                    }
                    g_variant_unref(wireless);
                }
                g_variant_unref(sDict);
                g_variant_unref(sSettingsRes);
            }
        }
        g_variant_unref(vList); 
        g_variant_unref(availRes);
    }

    // --- Step 3: Get Access Points ---
    GVariant *apRes = g_dbus_connection_call_sync(m_conn, "org.freedesktop.NetworkManager", wifiDevicePath.toUtf8().constData(),
        "org.freedesktop.NetworkManager.Device.Wireless", "GetAccessPoints", nullptr, G_VARIANT_TYPE("(ao)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);

    if (apRes) {
        GVariantIter *apIter;
        const gchar *apPath;
        g_variant_get(apRes, "(ao)", &apIter);
        while (g_variant_iter_loop(apIter, "o", &apPath)) {
            GVariant *props = g_dbus_connection_call_sync(m_conn, "org.freedesktop.NetworkManager", apPath, "org.freedesktop.DBus.Properties", 
                "GetAll", g_variant_new("(s)", "org.freedesktop.NetworkManager.AccessPoint"), G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
            
            if (props) {
                QVariantMap apMap;
                GVariantIter *pIter;
                g_variant_get(props, "(a{sv})", &pIter);
                
                const gchar *pKey;
                GVariant *pVal;
                QString currentSsid; 

                // 1. Collect ALL standard D-Bus properties
                while (g_variant_iter_next(pIter, "{sv}", &pKey, &pVal)) {
                    QVariant qv = gvariantToQVariant(pVal);
                    apMap[QString::fromUtf8(pKey)] = qv;
                    
                    // Capture SSID for the "Saved" check
                    if (g_strcmp0(pKey, "Ssid") == 0) {
                        currentSsid = qv.toString();
                    }
                    g_variant_unref(pVal);
                }

                // 2. Add extra metadata fields on top
                apMap["AccessPointPath"] = QString::fromUtf8(apPath);
                if (!currentSsid.isEmpty() && savedSsids.contains(currentSsid)) {
                    apMap["Saved"] = true;
                    apMap["SettingsPath"] = savedSsids[currentSsid];
                } else {
                    apMap["Saved"] = false;
                    apMap["SettingsPath"] = ""; 
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

void NetworkControl::ActivateConnection(const QString &devicePath, const QString &connectionPath, const QString &specificObjectPath) {
    GError *error = nullptr;

    g_dbus_connection_call_sync(m_conn,
        "org.freedesktop.NetworkManager",        
        "/org/freedesktop/NetworkManager",
        "org.freedesktop.NetworkManager",
        "ActivateConnection",
        g_variant_new("(ooo)", NULL,
            connectionPath.toUtf8().constData(),
            devicePath.toUtf8().constData(),
            specificObjectPath.toUtf8().constData()
        ),
        NULL,
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