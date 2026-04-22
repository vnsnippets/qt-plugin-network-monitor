### Contributing to NetworkMonitorPlugin

Thanks for taking the time to contribute!
Whether you are fixing a bug, improving the documentation, or adding support for a new feature, your help is appreciated. Even just raising issues helps to be honest.

---

### 🛠️ Development Environment

To hack on this plugin, you'll need the following installed on your system (Arch Linux naming):
* `qt6-base`, `qt6-declarative`
* `glib2`
* `cmake`, `pkg-config`
* `base-devel`

### 📜 Code of Conduct (The Basics)
- Be respectful to others.
- Keep discussions focused on the technical improvement of the plugin.
- This is a project for the Arch Rice community; let's keep it lean and fast.

### 🛠️ Technical Guidelines

This project bridges **GLib/GIO** (C style) and **Qt/QML** (C++ style).   
Please follow these rules to keep the plugin stable:

#### 1. Memory Management (The GVariant Rule)
Always ensure that `GVariant` objects are properly unreferenced using `g_variant_unref()` once they are no longer needed. If you use an iterator, ensure it is freed. 
> We want this plugin to run in the background for weeks without eating RAM.

#### 2. Thread Safety & Meta-Object System
All D-Bus callbacks occur on the GLib main context. To update QML properties or emit signals safely:
- Use `QMetaObject::invokeMethod` with `Qt::QueuedConnection`.
- Always capture values (integers, strings) by copy in lambdas, **never** pass raw `GVariant` pointers across threads.

#### 3. Formatting
- Use 4 spaces for indentation.
- Follow the existing naming convention: `PascalCase` for methods/slots, `m_camelCase` for private members.

#### 4. DBus Connection Management (Singleton)
To keep the plugin lightweight and prevent redundant socket usage, we use a singleton pattern for the DBus connection via `DBusFactory`.
- Use the shared connection: **Never** call `g_bus_get_sync` or `g_bus_get_finish` inside your own classes, and use `DBusFactory::connection()` instead.
- System Bus only: Network Manager lives on the system bus, so do not attempt to use the session bus.
- Lifecycle: The shared connection is owned by the `DBusFactory`. Do not `unref` or close the connection returned by the factory as other parts of the plugin depend on it.

```cpp
// Example: Correct way to access the bus
m_dbusConn = DBusFactory::connection();
if (!m_dbusConn) {
    qCritical() << "Critical: Shared DBus connection is unavailable.";
    return;
}
```

## 🚀 How to Submit a Change

1. **Fork the repo** and create your branch from `master`.
2. **Test your changes** locally by building with CMake and checking the output in a QML test file.
3. **Update the Documentation** if you added a new `Q_PROPERTY` or `Q_INVOKABLE` method.
4. **Open a Pull Request** with a clear description of:
   - What was broken or what feature was added.
   - Any specific NetworkManager version you tested against.

## 🐛 Reporting Bugs
When opening an issue, please include:
- Your distro (Arch, Artix, Fedora, etc.).
- Your desktop environment or Window Manager (Hyprland, Sway, KDE, etc.).
- The output of `journalctl -u NetworkManager --no-pager -n 50`.

---
*Happy Ricing!*