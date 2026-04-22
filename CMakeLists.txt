cmake_minimum_required(VERSION 3.16)
project(NetworkMonitorDBusPlugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable AUTOMOC for Qt macros (Q_OBJECT, Q_PLUGIN_METADATA)
set(CMAKE_AUTOMOC ON)

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS Qml)

# Find system dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0 gio-2.0)

# Define the shared library plugin
add_library(NetworkMonitorDBusPlugin SHARED
    NetworkMonitor.cpp
    NetworkControl.cpp
    Utilities.cpp
    plugin.cpp
)

# Include directories
target_include_directories(NetworkMonitorDBusPlugin PRIVATE 
    ${GLIB_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link libraries
# Use Qt6::Qml and GLIB libraries
target_link_libraries(NetworkMonitorDBusPlugin PRIVATE 
    Qt6::Qml 
    ${GLIB_LIBRARIES}
)

# Optional: Ensure the library is named exactly libNetworkMonitorDBusPlugin.so
set_target_properties(NetworkMonitorDBusPlugin PROPERTIES 
    PREFIX "lib"
    SUFFIX ".so"
)
