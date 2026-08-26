cmake_minimum_required(VERSION 3.16)

# Fallback for local builds
if(NOT PROJECT_VERSION)
    set(PROJECT_VERSION "0.1.0")
endif()

project(NetworkMonitorPlugin VERSION ${PROJECT_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# 1. Find Qt6 components explicitly
find_package(Qt6 REQUIRED COMPONENTS Core Qml)
if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

# 2. Find system dependencies (GIO is what handles the D-Bus calls)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0 gio-2.0)

# 3. Use the modern QML module command
# This replaces add_library, the manual qmldir, and manual registration.
set(module_path "NetworkMonitorPlugin")

qt_add_qml_module(NetworkMonitorPlugin
    URI "NetworkMonitorPlugin"
    VERSION 1.0
    SOURCES
        NetworkMonitor.h NetworkMonitor.cpp
        NetworkControl.h NetworkControl.cpp
        BluetoothMonitor.h BluetoothMonitor.cpp
        DBusFactory.h DBusFactory.cpp
        Utilities.h Utilities.cpp
)

# 4. Apply RPATH to both target libraries
# We use '\$ORIGIN' to ensure the $ makes it into the final binary
set(targets NetworkMonitorPlugin NetworkMonitorPluginplugin)
foreach(target ${targets})
    if(TARGET ${target})
        set_target_properties(${target} PROPERTIES
            SKIP_BUILD_RPATH FALSE
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "\$ORIGIN"
        )
    endif()
endforeach()

# 5. Include directories
target_include_directories(NetworkMonitorPlugin PRIVATE 
    ${GLIB_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 6. Link libraries
target_link_libraries(NetworkMonitorPlugin PRIVATE 
    Qt6::Core
    Qt6::Qml 
    ${GLIB_LIBRARIES}
)

# 7. Refined Library Properties
# IMPORTANT: Ensure the plugin target also uses the same output name logic
set_target_properties(NetworkMonitorPlugin PROPERTIES 
    PREFIX "lib"
    LIBRARY_OUTPUT_NAME "NetworkMonitorPlugin"
)

# Force the plugin to have the correct RPATH settings as well
if(TARGET NetworkMonitorPluginplugin)
    set_target_properties(NetworkMonitorPluginplugin PROPERTIES
        PREFIX "lib"
        INSTALL_RPATH "\$ORIGIN"
    )
endif()