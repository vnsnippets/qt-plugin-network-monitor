#include "NetworkControl.h"
#include "DBusFactory.h"
#include "Utilities.h"
#include <QDateTime>
#include <QDebug>

#undef signals
#include <gio/gio.h>

#define signals Q_SIGNALS

NetworkControl::NetworkControl(QObject *parent) : QObject(parent) {    
    m_dbusConn = DBusFactory::connection();
    if (!m_dbusConn) {
        qWarning("NetworkControl: Failed to get shared DBus connection.");
    }
}

NetworkControl::~NetworkControl() {
    m_dbusConn = nullptr;
}

QVariantMap NetworkControl::GetActiveDevice() {
    QVariantMap details;
    if (!m_dbusConn) return details;

    // 1. Get the 'PrimaryConnection' object path from the main NM interface
    GError *error = nullptr;
    GVariant *vPrimary = g_dbus_connection_call_sync(m_dbusConn,
        "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager",
        "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", "org.freedesktop.NetworkManager", "PrimaryConnection"),
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    
    if (!vPrimary) {
        if (error) {
            qWarning() << "GetActiveDevice: Failed to get PrimaryConnection:" << error->message;
            g_error_free(error);
        }
        return details;  // Return empty map on error
    }

    GVariant *vInnerPath = nullptr;
    // Validate format before unpacking
    if (!g_variant_is_of_type(vPrimary, G_VARIANT_TYPE("(v)"))) {
        qWarning() << "GetActiveDevice: PrimaryConnection has unexpected variant type";
        g_variant_unref(vPrimary);
        return details;
    }
    
    g_variant_get(vPrimary, "(v)", &vInnerPath);
    if (!vInnerPath) {
        qWarning() << "GetActiveDevice: Failed to unpack PrimaryConnection variant";
        g_variant_unref(vPrimary);
        return details;
    }
    
    const gchar *activeConnPath = g_variant_get_string(vInnerPath, nullptr);
    if (!activeConnPath) {
        // Unpack failed; return empty
        g_variant_unref(vInnerPath);
        g_variant_unref(vPrimary);
        return details;
    }

    // NetworkManager returns "/" if there is no active connection
    if (g_strcmp0(activeConnPath, "/") != 0) {
        // 2. Get the list of devices associated with this active connection
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

                    // 3. Take the first device path and fetch all its properties
                    if (g_variant_iter_loop(iter, "o", &devicePath) && devicePath) {
                        GVariant *allProps = g_dbus_connection_call_sync(m_dbusConn,
                            "org.freedesktop.NetworkManager", devicePath,
                            "org.freedesktop.DBus.Properties", "GetAll",
                            g_variant_new("(s)", "org.freedesktop.NetworkManager.Device"),
                            G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

                        if (allProps) {
                            GVariant *propDict = nullptr;
                            // Validate format before unpacking
                            if (g_variant_is_of_type(allProps, G_VARIANT_TYPE("(a{sv})"))) {
                                g_variant_get(allProps, "(@a{sv})", &propDict);
                            }
                            
                            if (propDict) {
                                GVariantIter pIter;
                                g_variant_iter_init(&pIter, propDict);
                                const gchar *key;
                                GVariant *val;

                                while (g_variant_iter_next(&pIter, "{sv}", &key, &val)) {
                                    details.insert(QString::fromUtf8(key), gvariantToQVariant(val));
                                    g_variant_unref(val);
                                }
                                details["DevicePath"] = QString::fromUtf8(devicePath);
                                
                                g_variant_unref(propDict);
                            }
                            g_variant_unref(allProps);
                        } else if (error) {
                            qWarning() << "GetActiveDevice: Failed to get device properties:" << error->message;
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
            qWarning() << "GetActiveDevice: Failed to get Devices:" << error->message;
            g_error_free(error);
            error = nullptr;
        }
    }

    // Clean up PrimaryConnection variants
    g_variant_unref(vInnerPath);
    g_variant_unref(vPrimary);

    return details;
}

// This returns EVERY device NM knows about, regardless of state
QList<QVariantMap> NetworkControl::GetAllDevices() {
    QList<QVariantMap> devices;
    if (!m_dbusConn) return devices;

    GError *error = nullptr;
    GVariant *result = g_dbus_connection_call_sync(m_dbusConn,
        "org.freedesktop.NetworkManager",        
        "/org/freedesktop/NetworkManager",
        "org.freedesktop.NetworkManager",
        "GetDevices",
        nullptr,
        G_VARIANT_TYPE("(ao)"),
        G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    
    if (!result) {
        if (error) {
            qWarning() << "GetAllDevices failed:" << error->message;
            g_error_free(error);
        }
        return devices;
    }

    GVariantIter *iter;
    const gchar *devPath;
    g_variant_get(result, "(ao)", &iter);
    
    // Process each path found
    while (g_variant_iter_loop(iter, "o", &devPath)) {
        devices.append(GetDeviceProperties(devPath));
    }

    g_variant_iter_free(iter);
    g_variant_unref(result);
    return devices;
}

QVariantMap NetworkControl::GetSettings(const QString &settingPath) {
    QVariantMap settings;
    if (!m_dbusConn || settingPath.isEmpty()) return settings;

    GError *error = nullptr;
    GVariant *result = g_dbus_connection_call_sync(m_dbusConn,
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
    qDebug() << "Request scan triggered (Async)";

    g_dbus_connection_call(m_dbusConn,
        "org.freedesktop.NetworkManager",
        devicePath.toUtf8().constData(),
        "org.freedesktop.NetworkManager.Device.Wireless",
        "RequestScan",
        g_variant_new("(a{sv})", NULL),
        NULL, // No return type needed
        G_DBUS_CALL_FLAGS_NONE,
        -1, nullptr,
        [](GObject* source, GAsyncResult* res, gpointer user_data) {
             GError *error = nullptr;
             g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &error);
             if (error) {
                 qWarning() << "Scan request failed:" << error->message;
                 g_error_free(error);
             }
        }, nullptr);
}

QList<QVariantMap> NetworkControl::GetAccessPoints(const QString &devicePath) {
    QList<QVariantMap> aps;
    if (!m_dbusConn || devicePath.isEmpty()) return aps;

    GError *error = nullptr;
    GVariant *result = g_dbus_connection_call_sync(m_dbusConn,
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
        GVariant *props = g_dbus_connection_call_sync(m_dbusConn,
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
    if (!m_dbusConn || wifiDevicePath.isEmpty()) return result;

    QMap<QString, QString> savedSsids;
    QMap<QString, quint64> savedTimestamps;

    // --- Step 1: Get Available Connections (Saved Networks) ---
    GError *error = nullptr;
    GVariant *availRes = g_dbus_connection_call_sync(m_dbusConn, "org.freedesktop.NetworkManager", 
        wifiDevicePath.toUtf8().constData(), "org.freedesktop.DBus.Properties", "Get", 
        g_variant_new("(ss)", "org.freedesktop.NetworkManager.Device", "AvailableConnections"), 
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

    if (availRes) {
        GVariant *vList = nullptr;
        // Validate format before unpacking
        if (g_variant_is_of_type(availRes, G_VARIANT_TYPE("(v)"))) {
            g_variant_get(availRes, "(v)", &vList);
        }
        
        if (vList && g_variant_is_of_type(vList, G_VARIANT_TYPE("ao"))) {
            GVariantIter iter; 
            g_variant_iter_init(&iter, vList);
            const gchar *statePath;
            
            while (g_variant_iter_loop(&iter, "o", &statePath)) {
                GVariant *sSettingsRes = g_dbus_connection_call_sync(m_dbusConn, "org.freedesktop.NetworkManager", statePath, 
                    "org.freedesktop.NetworkManager.Settings.Connection", "GetSettings", nullptr, 
                    G_VARIANT_TYPE("(a{sa{sv}})"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
                
                if (sSettingsRes) {
                    GVariant *sDict = nullptr;
                    // Validate format before unpacking
                    if (g_variant_is_of_type(sSettingsRes, G_VARIANT_TYPE("(a{sa{sv}})"))) {
                        g_variant_get(sSettingsRes, "(@a{sa{sv}})", &sDict);
                    }

                    if (sDict) {
                        // Look for Wireless section
                        GVariant *wireless = g_variant_lookup_value(sDict, "802-11-wireless", G_VARIANT_TYPE("a{sv}"));
                        if (wireless) {
                            GVariant *vSsid = g_variant_lookup_value(wireless, "ssid", G_VARIANT_TYPE("ay"));
                            if (vSsid) {
                                gsize len;
                                const guint8 *bytes = (const guint8*)g_variant_get_fixed_array(vSsid, &len, 1);
                                QString ssidName = QString::fromUtf8((const char*)bytes, (int)len);
                                savedSsids[ssidName] = QString::fromUtf8(statePath);
                                g_variant_unref(vSsid);
                            }
                            g_variant_unref(wireless);
                        }

                        // Look for Timestamp in Connection section
                        GVariant *connSection = g_variant_lookup_value(sDict, "connection", G_VARIANT_TYPE("a{sv}"));
                        if (connSection) {
                            GVariant *vTime = g_variant_lookup_value(connSection, "timestamp", G_VARIANT_TYPE("t"));
                            if (vTime) {
                                // Use currentSsidName if we found it above
                                // (Requires logic to link the two, simplified here)
                                g_variant_unref(vTime);
                            }
                            g_variant_unref(connSection);
                        }

                        g_variant_unref(sDict);
                    }
                    g_variant_unref(sSettingsRes);
                }
            }
        }
        if (vList) g_variant_unref(vList);
        g_variant_unref(availRes);
    } else if (error) {
        qWarning() << "GetKnownNetworksInRange: Failed to get AvailableConnections:" << error->message;
        g_error_free(error);
        error = nullptr;
    }

    // --- Step 2: Get Access Points (Scanning Results) ---
    GVariant *apRes = g_dbus_connection_call_sync(m_dbusConn, "org.freedesktop.NetworkManager", 
        wifiDevicePath.toUtf8().constData(), "org.freedesktop.NetworkManager.Device.Wireless", 
        "GetAccessPoints", nullptr, G_VARIANT_TYPE("(ao)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

    if (apRes) {
        GVariantIter *apIter = nullptr;
        // Validate format before unpacking
        if (g_variant_is_of_type(apRes, G_VARIANT_TYPE("(ao)"))) {
            g_variant_get(apRes, "(ao)", &apIter);
        }
        
        if (apIter) {
            const gchar *apPath;
            while (g_variant_iter_loop(apIter, "o", &apPath)) {
                GVariant *props = g_dbus_connection_call_sync(m_dbusConn, "org.freedesktop.NetworkManager", 
                    apPath, "org.freedesktop.DBus.Properties", "GetAll", 
                    g_variant_new("(s)", "org.freedesktop.NetworkManager.AccessPoint"),
                    G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);

                if (props) {
                    QVariantMap apMap;
                    GVariantIter *pIter = nullptr;
                    
                    // Validate format before unpacking
                    if (g_variant_is_of_type(props, G_VARIANT_TYPE("(a{sv})"))) {
                        g_variant_get(props, "(a{sv})", &pIter); // ALLOCATES pIter
                    }
                    
                    if (pIter) {
                        const gchar *pKey;
                        GVariant *pVal;
                        QString currentSsid;
                        
                        while (g_variant_iter_next(pIter, "{sv}", &pKey, &pVal)) {
                            apMap[QString::fromUtf8(pKey)] = gvariantToQVariant(pVal);
                            if (g_strcmp0(pKey, "Ssid") == 0) {
                                currentSsid = apMap[pKey].toString();
                            }
                            g_variant_unref(pVal);
                        }

                        apMap["AccessPointPath"] = QString::fromUtf8(apPath);
                        bool isSaved = !currentSsid.isEmpty() && savedSsids.contains(currentSsid);
                        apMap["Saved"] = isSaved;
                        apMap["SettingsPath"] = isSaved ? savedSsids[currentSsid] : "";

                        result.append(apMap);

                        g_variant_iter_free(pIter); // CRITICAL: Fixes Memory Leak
                    }
                    g_variant_unref(props);
                }
            }
            g_variant_iter_free(apIter);
        }
        g_variant_unref(apRes);
    } else if (error) {
        qWarning() << "GetKnownNetworksInRange: Failed to get AccessPoints:" << error->message;
        g_error_free(error);
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

    g_dbus_connection_call_sync(m_dbusConn,
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
    if (!m_dbusConn || devicePath.isEmpty()) return;

    GError *error = nullptr;

    // We call Disconnect on the Device interface. 
    // This works for both org.freedesktop.NetworkManager.Device.Wireless 
    // and org.freedesktop.NetworkManager.Device.Wired.

    // This tells the hardware to disconnect.
    // Crucially, it also prevents the device from auto-connecting to that same
    // network again until you manually intervene or reboot.

    g_dbus_connection_call_sync(m_dbusConn,
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

QVariantMap NetworkControl::GetDeviceProperties(const QString &devicePath) {
    QVariantMap info;
    if (!m_dbusConn || devicePath.isEmpty() || devicePath == "/") return info;

    info["DevicePath"] = devicePath;

    GVariant *props = g_dbus_connection_call_sync(m_dbusConn,
        "org.freedesktop.NetworkManager",        
        devicePath.toUtf8().constData(),
        "org.freedesktop.DBus.Properties",
        "GetAll",
        g_variant_new("(s)", "org.freedesktop.NetworkManager.Device"),
        G_VARIANT_TYPE("(a{sv})"),
        G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);

    if (props) {
        GVariantIter *dictIter;
        g_variant_get(props, "(a{sv})", &dictIter);

        const gchar *key;
        GVariant *val;
        while (g_variant_iter_next(dictIter, "{sv}", &key, &val)) {                    
            info[QString::fromUtf8(key)] = gvariantToQVariant(val);
            g_variant_unref(val); 
        }
        g_variant_iter_free(dictIter);
        g_variant_unref(props);
    }
    return info;
}