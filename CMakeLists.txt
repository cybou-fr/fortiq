cmake_minimum_required(VERSION 3.21)
project(Fortiq VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Quick QuickControls2 Network Concurrent)
qt_standard_project_setup(REQUIRES 6.8)

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

qt_add_qml_module(fortiq
    URI Fortiq.App
    VERSION 1.0
    QML_FILES qml/Main.qml
)

target_link_libraries(fortiq PRIVATE
    Qt6::Core Qt6::Quick Qt6::QuickControls2 Qt6::Network
    Qt6::Concurrent ${FORTIQ_SSH_TARGET})

set_target_properties(fortiq PROPERTIES
    WIN32_EXECUTABLE TRUE
    QT_ANDROID_PACKAGE_NAME "fr.cybou.fortiq"
    QT_ANDROID_APP_NAME "FORTIQ"
)

if(ANDROID)
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
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    qt_add_executable(fortiq_tests tests/RemotePathTest.cpp src/core/RemotePath.cpp src/core/RemotePath.h)
    target_link_libraries(fortiq_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME fortiq_remote_path COMMAND fortiq_tests)
endif()
