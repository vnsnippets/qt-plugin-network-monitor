#include "DBusFactory.h"
#include <QDebug>

#undef signals
#include <gio/gio.h>

#define signals Q_SIGNALS

GDBusConnection* DBusFactory::s_connection = nullptr;

GDBusConnection* DBusFactory::connection() {
    if (!s_connection) {
        GError *error = nullptr;
        s_connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
        if (error) {
            qWarning() << "Failed to connect to System Bus:" << error->message;
            g_error_free(error);
        }
    }
    return s_connection;
}

void DBusFactory::cleanup() {
    if (s_connection) {
        g_object_unref(s_connection);
        s_connection = nullptr;
    }
}