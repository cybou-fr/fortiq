cmake_minimum_required(VERSION 3.21)
project(Fortiq VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Concurrent)
qt_standard_project_setup(REQUIRES 6.8)

# One raster brand source -> native icons for both executables and the Android APK.
# Python stdlib generator is deterministic and does not require Pillow.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(FORTIQ_BRAND_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/branding")
set(FORTIQ_ANDROID_PACKAGE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/android")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/fortiq_mark.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/tool/generate_brand_assets.py"
)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tool/generate_brand_assets.py"
        --source "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/fortiq_mark.png"
        --output "${FORTIQ_BRAND_OUTPUT}"
        --android-output "${FORTIQ_ANDROID_PACKAGE_OUTPUT}"
    RESULT_VARIABLE FORTIQ_BRAND_RESULT
    OUTPUT_VARIABLE FORTIQ_BRAND_LOG
    ERROR_VARIABLE FORTIQ_BRAND_ERROR
)
if(NOT FORTIQ_BRAND_RESULT EQUAL 0)
    message(FATAL_ERROR "FORTIQ brand icons could not be generated: ${FORTIQ_BRAND_ERROR}")
endif()
message(STATUS "${FORTIQ_BRAND_LOG}")

# CMake compiles .rc with rc.exe (MSVC) or windres (MinGW), embedding .ico
# into the Windows executable itself (Explorer/taskbar icon).
if(WIN32)
    enable_language(RC)
endif()

# FORTIQ_SHELL_PREVIEW_ONLY builds just the Desktop Shell and the
# fortiq_shell_preview capture tool, without libssh2. It exists for the
# 1536x1024 golden-screenshot comparison and never ships to users.
option(FORTIQ_SHELL_PREVIEW_ONLY "Build only the shell preview (no SSH)" OFF)

set(FORTIQ_SHELL_QML
    qml/shell/Theme.qml
    qml/shell/Wallpaper.qml
    qml/shell/Glass.qml
    qml/shell/AppIcon.qml
    qml/shell/FolderGlyph.qml
    qml/shell/FileThumb.qml
    qml/shell/TopBar.qml
    qml/shell/AppRail.qml
    qml/shell/ClockWidget.qml
    qml/shell/ConnectPane.qml
    qml/shell/FilesWindow.qml
    qml/shell/LauncherPanel.qml
    qml/shell/BrowserWindow.qml
    qml/shell/ControlCenter.qml
    qml/shell/WorkspaceSwitcher.qml
    qml/shell/Dock.qml
    qml/shell/DesktopShell.qml
)
set_source_files_properties(qml/shell/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(fortiq_shell STATIC)
qt_add_qml_module(fortiq_shell
    URI Fortiq.Shell
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES ${FORTIQ_SHELL_QML}
    RESOURCES
        assets/wallpapers/aurora.jpg
        assets/wallpapers/desert.jpg
        assets/wallpapers/city.jpg
)
target_link_libraries(fortiq_shell PRIVATE Qt6::Quick)

qt_add_executable(fortiq_shell_preview src/main.cpp)
target_compile_definitions(fortiq_shell_preview PRIVATE FORTIQ_NO_SSH)
qt_add_resources(fortiq_shell_preview "fortiq_preview_icon"
    PREFIX "/branding"
    BASE "${FORTIQ_BRAND_OUTPUT}"
    FILES "${FORTIQ_BRAND_OUTPUT}/fortiq_mark.png")
if(WIN32)
    target_sources(fortiq_shell_preview PRIVATE "${FORTIQ_BRAND_OUTPUT}/fortiq.rc")
endif()
qt_add_qml_module(fortiq_shell_preview
    URI Fortiq.App
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/preview_qml/Fortiq/App
    QML_FILES qml/Main.qml
)
target_link_libraries(fortiq_shell_preview PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 fortiq_shellplugin)

if(FORTIQ_SHELL_PREVIEW_ONLY)
    return()
endif()

# libssh2 is the SAME SSH transport on Windows and Android. The chosen library
# must be built for the target ABI; never fall back to invoking a local shell.
find_package(libssh2 CONFIG QUIET)
if(TARGET Libssh2::libssh2)
    set(FORTIQ_SSH_TARGET Libssh2::libssh2)
elseif(TARGET libssh2::libssh2)
    set(FORTIQ_SSH_TARGET libssh2::libssh2)
elseif(TARGET unofficial-libssh2::libssh2)
    set(FORTIQ_SSH_TARGET unofficial-libssh2::libssh2)
else()
    find_path(FORTIQ_LIBSSH2_INCLUDE_DIR NAMES libssh2.h REQUIRED)
    find_library(FORTIQ_LIBSSH2_LIBRARY NAMES ssh2 libssh2 libssh2_static REQUIRED)
    add_library(fortiq_libssh2 UNKNOWN IMPORTED)
    set_target_properties(fortiq_libssh2 PROPERTIES
        IMPORTED_LOCATION "${FORTIQ_LIBSSH2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FORTIQ_LIBSSH2_INCLUDE_DIR}")
    set(FORTIQ_SSH_TARGET fortiq_libssh2)
endif()

qt_add_executable(fortiq
    src/main.cpp
    src/core/RemotePath.cpp
    src/core/RemotePath.h
    src/core/RemoteController.cpp
    src/core/RemoteController.h
    src/ssh/SshClient.cpp
    src/ssh/SshClient.h
)
qt_add_resources(fortiq "fortiq_app_icon"
    PREFIX "/branding"
    BASE "${FORTIQ_BRAND_OUTPUT}"
    FILES "${FORTIQ_BRAND_OUTPUT}/fortiq_mark.png")
if(WIN32)
    target_sources(fortiq PRIVATE "${FORTIQ_BRAND_OUTPUT}/fortiq.rc")
endif()

qt_add_qml_module(fortiq
    URI Fortiq.App
    VERSION 1.0
    QML_FILES qml/Main.qml
)

target_link_libraries(fortiq PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Network
    Qt6::Concurrent fortiq_shellplugin ${FORTIQ_SSH_TARGET})

set_target_properties(fortiq PROPERTIES
    WIN32_EXECUTABLE TRUE
    QT_ANDROID_PACKAGE_NAME "fr.cybou.fortiq"
    QT_ANDROID_APP_NAME "FORTIQ"
)

if(ANDROID)
    if(Qt6_VERSION VERSION_LESS "6.9.0")
        message(FATAL_ERROR "FORTIQ Android launcher/adaptive icon requires Qt 6.9 or newer")
    endif()
    set_target_properties(fortiq PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${FORTIQ_ANDROID_PACKAGE_OUTPUT}"
        QT_ANDROID_APP_ICON "@mipmap/ic_launcher"
    )
    # Qt6.9+ can add permissions to the generated manifest. QtNetwork also
    # declares INTERNET on Qt builds where the module requests it.
    if(COMMAND qt_add_android_permission)
        qt_add_android_permission(fortiq NAME android.permission.INTERNET)
    endif()
    # Shared libssh2/libcrypto/libssl binaries must be included with the APK,
    # or use statically linked cross-compiled libraries for the target ABI.
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME fortiq_brand_assets
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tool/check_brand_assets.py"
          --output "${FORTIQ_BRAND_OUTPUT}"
          --android-output "${FORTIQ_ANDROID_PACKAGE_OUTPUT}")
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    qt_add_executable(fortiq_tests tests/RemotePathTest.cpp src/core/RemotePath.cpp src/core/RemotePath.h)
    target_link_libraries(fortiq_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME fortiq_remote_path COMMAND fortiq_tests)
    # Live SSH check; skips itself unless FORTIQ_IT_* variables are set.
    qt_add_executable(fortiq_ssh_it tests/SshIntegrationTest.cpp
        src/ssh/SshClient.cpp src/ssh/SshClient.h src/core/RemotePath.cpp src/core/RemotePath.h)
    target_include_directories(fortiq_ssh_it PRIVATE src)
    target_link_libraries(fortiq_ssh_it PRIVATE Qt6::Core Qt6::Network Qt6::Test ${FORTIQ_SSH_TARGET})
    add_test(NAME fortiq_ssh_integration COMMAND fortiq_ssh_it)
endif()
