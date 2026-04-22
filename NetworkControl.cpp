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
    GError *error = nullptr;

    GVariant *result = g_dbus_connection_call_sync(m_conn,
        "org.freedesktop.NetworkManager",
        devicePath.toUtf8().constData(),
        "org.freedesktop.NetworkManager.Device.Wireless",
        "GetAccessPoints",
        NULL,
        G_VARIANT_TYPE("(ao)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        &error);
    
    if (result) {
        GVariantIter *iter;
        const gchar *apPath;
        g_variant_get(result, "(ao)", &iter);
        while (g_variant_iter_loop(iter, "o", &apPath)) {
            QVariantMap info;
            info["AccessPointPath"] = QString::fromUtf8(apPath);

            GVariant *props = g_dbus_connection_call_sync(m_conn,
                "org.freedesktop.NetworkManager",        
                apPath,
                "org.freedesktop.DBus.Properties",
                "GetAll",
                g_variant_new("(s)", "org.freedesktop.NetworkManager.AccessPoint"),
                G_VARIANT_TYPE("(a{sv})"),
                G_DBUS_CALL_FLAGS_NONE,
                -1,
                nullptr,
                &error);

            if (props) {
                GVariantIter *dictIter;
                const gchar *key;
                GVariant *val;
                g_variant_get(props, "(a{sv})", &dictIter);

                while (g_variant_iter_loop(dictIter, "{&s@v}", &key, &val)) {
                    info[key] = gvariantToQVariant(val);
                }

                g_variant_iter_free(dictIter);
                g_variant_unref(props);
            }

            aps.append(info);
        }

        g_variant_iter_free(iter);
        g_variant_unref(result);
    }

    return aps;
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