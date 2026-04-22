#pragma once
#include <QObject>

typedef struct _GDBusConnection GDBusConnection;

class DBusFactory {
    public:
        // Access the single instance of the connection
        static GDBusConnection* connection();
        
        // Cleanup when the plugin is unloaded
        static void cleanup();

    private:
        static GDBusConnection* s_connection;
};