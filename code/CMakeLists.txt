cmake_minimum_required(VERSION 3.16)

# Fallback for local builds
if(NOT PROJECT_VERSION)
    set(PROJECT_VERSION "0.1.0")
endif()

project(NetworkMonitorDBusPlugin VERSION ${PROJECT_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Essential for Q_OBJECT and signals
set(CMAKE_AUTOMOC ON)

# 1. Find Qt6 components explicitly
find_package(Qt6 REQUIRED COMPONENTS Core Qml)

# 2. Find system dependencies (GIO is what handles the D-Bus calls)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0 gio-2.0)

# 3. Define the shared library
add_library(NetworkMonitorDBusPlugin SHARED
    NetworkMonitor.cpp
    NetworkControl.cpp
    DBusFactory.cpp
    Utilities.cpp
    plugin.cpp
)

# 4. Include directories
target_include_directories(NetworkMonitorDBusPlugin PRIVATE 
    ${GLIB_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 5. Link libraries
target_link_libraries(NetworkMonitorDBusPlugin PRIVATE 
    Qt6::Core
    Qt6::Qml 
    ${GLIB_LIBRARIES}
)

# 6. Refined Library Properties
set_target_properties(NetworkMonitorDBusPlugin PROPERTIES 
    PREFIX "lib"
    LIBRARY_OUTPUT_NAME "NetworkMonitorDBusPlugin"
)

# 7. Create the manifest file required by QML
set(QMLDIR_FILE "${CMAKE_CURRENT_BINARY_DIR}/qmldir")
file(WRITE ${QMLDIR_FILE}
    "module NetworkMonitorDBusPlugin\n"
    "plugin NetworkMonitorDBusPlugin\n"
)

# 8. Copy qmldir to the build folder where the .so lives
add_custom_command(TARGET NetworkMonitorDBusPlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${QMLDIR_FILE}
    $<TARGET_FILE_DIR:NetworkMonitorDBusPlugin>
    COMMENT "Copying qmldir to build directory"
)