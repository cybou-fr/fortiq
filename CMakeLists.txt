cmake_minimum_required(VERSION 3.21)
project(Fortiq VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Concurrent)
file(GLOB FORTIQ_VTERM_SOURCES CONFIGURE_DEPENDS third_party/libvterm/src/*.c)
add_library(fortiq_vterm STATIC ${FORTIQ_VTERM_SOURCES})
target_include_directories(fortiq_vterm PUBLIC third_party/libvterm/include)
set_target_properties(fortiq_vterm PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
qt_standard_project_setup(REQUIRES 6.9)
# Set this when QML modules are declared, before Qt caches scan/deploy policy.
set(FORTIQ_QML_SCAN_OPTIONS)
if(WIN32 AND QT6_IS_SHARED_LIBS_BUILD)
    list(APPEND FORTIQ_QML_SCAN_OPTIONS NO_IMPORT_SCAN)
endif()

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
option(FORTIQ_FUZZ "Build the libFuzzer target (clang, with ASan/UBSan)" OFF)
option(FORTIQ_SANITIZE "Build everything with AddressSanitizer + UndefinedBehaviorSanitizer (clang/gcc)" OFF)
option(FORTIQ_SHELL_PREVIEW_ONLY "Build only the shell preview (no SSH)" OFF)
# Browser engine (Qt WebEngine = Chromium; docs/BROWSER.md, "Moteur"). Desktop only: Android uses the system WebView (later stage).
# OFF keeps the text reader only (curl on the server) and builds without the ~200 MB of Chromium.
if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(FORTIQ_WEBENGINE_DEFAULT OFF)
else()
    set(FORTIQ_WEBENGINE_DEFAULT ON)
endif()
option(FORTIQ_WITH_WEBENGINE "Build the Chromium engine window of the Browser (Qt WebEngine)" ${FORTIQ_WEBENGINE_DEFAULT})
# Android: the same Browser engine window over the SYSTEM WebView (Chromium), proxied through the SSH gateway (docs/BROWSER.md, "Android").
if(ANDROID)
    option(FORTIQ_WITH_ANDROID_WEBVIEW "Build the Browser engine window over the Android system WebView" ON)
else()
    set(FORTIQ_WITH_ANDROID_WEBVIEW OFF)
endif()
# Developer-only, never shipped: a debug entry (--browser-selftest) that loads pages through the gateway for tool/android_browser_check.py.
option(FORTIQ_ANDROID_BROWSER_SELFTEST "Build the Android browser self-test entry (developer APK only)" OFF)
if(FORTIQ_WITH_ANDROID_WEBVIEW AND FORTIQ_SHELL_PREVIEW_ONLY)
    set(FORTIQ_WITH_ANDROID_WEBVIEW OFF)
endif()
if(FORTIQ_WITH_WEBENGINE AND NOT FORTIQ_SHELL_PREVIEW_ONLY)
    find_package(Qt6 6.9 QUIET COMPONENTS WebEngineQuick)
    if(NOT TARGET Qt6::WebEngineQuick)
        message(WARNING "Qt WebEngine is not installed for this Qt kit: building WITHOUT the Browser engine (FORTIQ_WITH_WEBENGINE=OFF). "
                        "Install the 'Qt WebEngine' component with the Qt Maintenance Tool.")
        set(FORTIQ_WITH_WEBENGINE OFF)
    endif()
endif()
if(FORTIQ_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined -g)
    add_link_options(-fsanitize=address,undefined)
endif()

set(FORTIQ_SHELL_QML
    qml/shell/Theme.qml
    qml/shell/Wallpaper.qml
    qml/shell/Glass.qml
    qml/shell/WindowDragArea.qml
    qml/shell/TerminalWindow.qml
    qml/shell/DisksWindow.qml
    qml/shell/DocumentsWindow.qml
    qml/shell/AppIcon.qml
    qml/shell/FolderGlyph.qml
    qml/shell/FileThumb.qml
    qml/shell/FileIcon.qml
    qml/shell/FileViewer.qml
    qml/shell/TaskPanel.qml
    qml/shell/FixtureTasks.qml
    qml/shell/GlassDialog.qml
    qml/shell/SearchResults.qml
    qml/shell/LogsWindow.qml
    qml/shell/IncidentInspector.qml
    qml/shell/ServicePanel.qml
    qml/shell/ServerManager.qml
    qml/shell/AppPanel.qml
    qml/shell/ToolButtonGlass.qml
    qml/shell/SettingsWindow.qml
    qml/shell/SymbolGlyph.qml
    qml/shell/ToolTipLite.qml
    qml/shell/TopBar.qml
    qml/shell/ClockWidget.qml
    qml/shell/FilesWindow.qml
    qml/shell/FilesSidebar.qml
    qml/shell/FilesToolbar.qml
    qml/shell/FilesTrashBanner.qml
    qml/shell/FilesGrid.qml
    qml/shell/FilesList.qml
    qml/shell/FilesStatusBar.qml
    qml/shell/FilesFixtureData.qml
    qml/shell/LauncherPanel.qml
    qml/shell/BrowserWindow.qml
    qml/shell/WebWindow.qml
    qml/shell/NetworkWindow.qml
    qml/shell/SystemWindow.qml
    qml/shell/DbWindow.qml
    qml/shell/SessionGuard.qml
    qml/shell/LinkBanner.qml
    qml/shell/CopyChip.qml
    qml/shell/Toast.qml
    qml/shell/MailWindow.qml
    qml/shell/ControlCenter.qml
    qml/shell/WorkspaceSwitcher.qml
    qml/shell/AdaptiveFilesToolbar.qml
    qml/shell/CompactAppPanel.qml
    qml/shell/DesktopShell.qml
)
set_source_files_properties(qml/shell/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(fortiq_shell STATIC)
qt_add_qml_module(fortiq_shell ${FORTIQ_QML_SCAN_OPTIONS}
    URI Fortiq.Shell
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/Fortiq/Shell
    QML_FILES ${FORTIQ_SHELL_QML}
    SOURCES src/terminal/TerminalView.cpp src/terminal/TerminalView.h
        src/documents/MarkdownPreview.cpp src/documents/MarkdownPreview.h
        src/documents/MarkdownHighlighter.cpp src/documents/MarkdownHighlighter.h
        src/documents/DocumentSearch.cpp src/documents/DocumentSearch.h
    RESOURCES
        assets/wallpapers/aurora.jpg
        assets/wallpapers/desert.jpg
        assets/wallpapers/city.jpg
)
target_link_libraries(fortiq_shell PRIVATE Qt6::Quick fortiq_vterm)
target_include_directories(fortiq_shell PRIVATE src/terminal src/documents)

qt_add_executable(fortiq_shell_preview src/main.cpp)
target_compile_definitions(fortiq_shell_preview PRIVATE FORTIQ_NO_SSH)
qt_add_resources(fortiq_shell_preview "fortiq_preview_icon"
    PREFIX "/branding"
    BASE "${FORTIQ_BRAND_OUTPUT}"
    FILES "${FORTIQ_BRAND_OUTPUT}/fortiq_mark.png")
if(WIN32)
    target_sources(fortiq_shell_preview PRIVATE "${FORTIQ_BRAND_OUTPUT}/fortiq.rc")
endif()
qt_add_qml_module(fortiq_shell_preview ${FORTIQ_QML_SCAN_OPTIONS}
    URI Fortiq.App
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/preview_qml/Fortiq/App
    IMPORT_PATH ${CMAKE_CURRENT_BINARY_DIR}/qml
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

set(FORTIQ_LOGS_SOURCES
    src/core/LogsController.cpp
    src/core/LogsAnalysis.cpp
    src/core/LogsInspector.cpp
    src/core/LogsServices.cpp
)

set(FORTIQ_BROWSER_PURE_SOURCES
    src/browser/UrlTools.cpp
    src/browser/CurlFetch.cpp
    src/browser/HtmlSanitizer.cpp
    src/browser/UrlPolicy.cpp
)
# SSH proxy gateway of the Browser (docs/BROWSER.md, "Passerelle SSH"): pure part (HTTP wire, proxy, address policy; no libssh2) and the tunnel.
set(FORTIQ_GATEWAY_PURE_SOURCES
    src/browser/gateway/HttpWire.cpp src/browser/gateway/HttpWire.h
    src/browser/gateway/TunnelBackend.h
    src/browser/gateway/ProxyGateway.cpp src/browser/gateway/ProxyGateway.h
    src/browser/UrlPolicy.cpp src/browser/UrlPolicy.h
)
set(FORTIQ_GATEWAY_TUNNEL_SOURCES src/browser/gateway/SshTunnel.cpp src/browser/gateway/SshTunnel.h src/ssh/SshAlgorithms.cpp src/ssh/SshAlgorithms.h)
# The engine's own logic (profile rules, tunnel + gateway lifecycle, credentials, address question): no Qt WebEngine needed to build it.
set(FORTIQ_ENGINE_PURE_SOURCES src/browser/engine/EngineProfile.cpp src/browser/engine/EngineProfile.h src/browser/engine/TabModel.cpp src/browser/engine/TabModel.h)
set(FORTIQ_ENGINE_SOURCES ${FORTIQ_ENGINE_PURE_SOURCES} src/browser/engine/EngineBrowser.cpp src/browser/engine/EngineBrowser.h)
set(FORTIQ_ENGINE_APP_SOURCES src/browser/engine/EngineRemote.cpp)
set(FORTIQ_BROWSER_SOURCES ${FORTIQ_BROWSER_PURE_SOURCES} src/browser/BrowserController.cpp src/browser/BrowserController.h
    ${FORTIQ_GATEWAY_PURE_SOURCES} ${FORTIQ_GATEWAY_TUNNEL_SOURCES} ${FORTIQ_ENGINE_SOURCES} ${FORTIQ_ENGINE_APP_SOURCES})
list(REMOVE_DUPLICATES FORTIQ_BROWSER_SOURCES)

set(FORTIQ_NETWORK_PURE_SOURCES src/network/NetworkInfo.cpp src/network/NetworkInfo.h)
set(FORTIQ_NETWORK_SOURCES ${FORTIQ_NETWORK_PURE_SOURCES} src/network/NetworkController.cpp src/network/NetworkController.h)
set(FORTIQ_SYSTEM_PURE_SOURCES src/system/SystemInfo.cpp src/system/SystemInfo.h)
set(FORTIQ_SYSTEM_SOURCES ${FORTIQ_SYSTEM_PURE_SOURCES} src/system/SystemController.cpp src/system/SystemController.h)
set(FORTIQ_MAIL_PURE_SOURCES src/mail/MailInfo.cpp src/mail/MailInfo.h src/mail/MailSend.cpp src/mail/MailSend.h src/mail/OutgoingAttachment.cpp src/mail/OutgoingAttachment.h src/mail/MailAccount.cpp src/mail/MailAccount.h src/mail/MailAttachment.cpp src/mail/MailAttachment.h)
set(FORTIQ_MAIL_SOURCES ${FORTIQ_MAIL_PURE_SOURCES} src/mail/MailController.cpp src/mail/MailController.h)
set(FORTIQ_DB_PURE_SOURCES src/db/DbInfo.cpp src/db/DbInfo.h)
set(FORTIQ_DB_SOURCES ${FORTIQ_DB_PURE_SOURCES} src/db/DbController.cpp src/db/DbController.h)

set(FORTIQ_REMOTE_SOURCES
    src/core/RemoteController.cpp
    src/core/RemoteProfiles.cpp
    src/core/RemotePreview.cpp
    src/core/RemoteTransfers.cpp
    src/core/RemoteOperations.cpp
    src/core/RemoteSearch.cpp
)

set(FORTIQ_SSH_SOURCES
    src/ssh/LinkMonitor.cpp
    src/ssh/LinkMonitor.h
    src/ssh/SshSession.cpp
    src/ssh/SshTerminal.cpp
    src/ssh/SshTransfers.cpp
    src/ssh/SshFileOps.cpp
    src/ssh/SshTrash.cpp
    src/ssh/SshEdit.cpp
    src/ssh/SshExec.cpp
    src/ssh/SshAlgorithms.cpp
    src/ssh/SshAlgorithms.h
)

qt_add_executable(fortiq
    src/main.cpp
    src/core/LocalDocument.cpp
    src/core/LocalDocument.h
    src/core/RemotePath.cpp
    src/core/RemotePath.h
    ${FORTIQ_REMOTE_SOURCES}
    src/core/RemoteController.h
    src/core/SecretStore.cpp
    src/core/SecretStore.h
    src/core/TaskManager.cpp
    src/core/TaskManager.h
    ${FORTIQ_BROWSER_SOURCES}
    ${FORTIQ_NETWORK_SOURCES}
    ${FORTIQ_SYSTEM_SOURCES}
    ${FORTIQ_DB_SOURCES}
    ${FORTIQ_MAIL_SOURCES}
    ${FORTIQ_LOGS_SOURCES}
    src/core/LogsController.h
    src/core/TerminalController.cpp
    src/core/TerminalController.h
    src/core/DisksController.cpp
    src/core/DisksController.h
    src/core/DocumentsController.cpp
    src/core/DocumentsController.h
    ${FORTIQ_SSH_SOURCES}
    src/ssh/SshClient.h
)
qt_add_resources(fortiq "fortiq_app_icon"
    PREFIX "/branding"
    BASE "${FORTIQ_BRAND_OUTPUT}"
    FILES "${FORTIQ_BRAND_OUTPUT}/fortiq_mark.png")
qt_add_resources(fortiq "fortiq_third_party" PREFIX "/licenses"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvterm"
    FILES third_party/libvterm/LICENSE)
if(WIN32)
    target_sources(fortiq PRIVATE "${FORTIQ_BRAND_OUTPUT}/fortiq.rc")
    # The .rc only names fortiq.ico: without this the resource is not recompiled when the icon changes (the exe kept the old icon).
    set_source_files_properties("${FORTIQ_BRAND_OUTPUT}/fortiq.rc" PROPERTIES OBJECT_DEPENDS "${FORTIQ_BRAND_OUTPUT}/fortiq.ico")
endif()

qt_add_qml_module(fortiq ${FORTIQ_QML_SCAN_OPTIONS}
    URI Fortiq.App
    VERSION 1.0
    IMPORT_PATH ${CMAKE_CURRENT_BINARY_DIR}/qml
    QML_FILES qml/Main.qml
)

target_link_libraries(fortiq PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Network
    Qt6::Concurrent fortiq_shellplugin ${FORTIQ_SSH_TARGET})

if(FORTIQ_WITH_WEBENGINE OR FORTIQ_WITH_ANDROID_WEBVIEW)
    # The web view lives in a QML module of its own so that builds without an engine never import QtWebEngine.
    qt_add_library(fortiq_engine STATIC)
    if(FORTIQ_WITH_ANDROID_WEBVIEW)
        # The page is the system WebView: same EngineWindow.qml, a different EngineView.qml (published under the same name).
        set_source_files_properties(qml/engine/android/EngineView.qml PROPERTIES QT_RESOURCE_ALIAS EngineView.qml)
        set_source_files_properties(qml/engine/android/PageProfile.qml PROPERTIES QT_RESOURCE_ALIAS PageProfile.qml)
        set(FORTIQ_ENGINE_VIEW_QML qml/engine/android/EngineView.qml qml/engine/android/PageProfile.qml)
        set(FORTIQ_ENGINE_VIEW_SOURCES src/browser/engine/android/AndroidWebView.cpp src/browser/engine/android/AndroidWebView.h)
    else()
        set(FORTIQ_ENGINE_VIEW_QML qml/engine/EngineView.qml qml/engine/PageProfile.qml)
        set(FORTIQ_ENGINE_VIEW_SOURCES)
    endif()
    qt_add_qml_module(fortiq_engine ${FORTIQ_QML_SCAN_OPTIONS}
        URI Fortiq.Engine
        VERSION 1.0
        RESOURCE_PREFIX /qt/qml
        OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/Fortiq/Engine
        IMPORT_PATH ${CMAKE_CURRENT_BINARY_DIR}/qml
        QML_FILES ${FORTIQ_ENGINE_VIEW_QML} qml/engine/EngineWindow.qml qml/engine/EnginePages.qml
        SOURCES ${FORTIQ_ENGINE_VIEW_SOURCES}
    )
    target_include_directories(fortiq_engine PRIVATE src src/browser/engine/android)   # the generated type registration includes <AndroidWebView.h>
    if(FORTIQ_WITH_WEBENGINE)
        target_link_libraries(fortiq_engine PRIVATE Qt6::Quick Qt6::WebEngineQuick fortiq_shellplugin)
        target_compile_definitions(fortiq PRIVATE FORTIQ_HAVE_WEBENGINE)
        target_link_libraries(fortiq PRIVATE Qt6::WebEngineQuick fortiq_engineplugin)
    else()
        target_link_libraries(fortiq_engine PRIVATE Qt6::Quick Qt6::Qml fortiq_shellplugin)
        target_compile_definitions(fortiq PRIVATE FORTIQ_HAVE_ANDROID_WEBVIEW)
        target_compile_definitions(fortiq_engine PRIVATE FORTIQ_HAVE_ANDROID_WEBVIEW)
        target_link_libraries(fortiq PRIVATE fortiq_engineplugin)
        if(FORTIQ_ANDROID_BROWSER_SELFTEST)
            target_compile_definitions(fortiq PRIVATE FORTIQ_ANDROID_BROWSER_SELFTEST)
            target_sources(fortiq PRIVATE src/browser/engine/android/BrowserSelfTest.cpp src/browser/engine/android/BrowserSelfTest.h)
        endif()
    endif()
endif()

set_target_properties(fortiq PROPERTIES
    WIN32_EXECUTABLE TRUE
    QT_ANDROID_PACKAGE_NAME "fr.cybou.fortiq"
    QT_ANDROID_APP_NAME "FORTIQ"
)

# Licensing (audit backlog 10): the Qt Virtual Keyboard module is GPL-3.0-only or commercial (no LGPL), and FORTIQ does not use it; the
# platform input context plugin that pulls it in must never ship. Lottie vector images are not used either.
qt_import_plugins(fortiq EXCLUDE_BY_TYPE platforminputcontexts EXCLUDE_BY_TYPE vectorimageformats)
if(ANDROID)
    if(Qt6_VERSION VERSION_LESS "6.9.0")
        message(FATAL_ERROR "FORTIQ Android launcher/adaptive icon requires Qt 6.9 or newer")
    endif()
    set(FORTIQ_ANDROID_SDK "36" CACHE STRING "Stable Android compile/target SDK")
    configure_file(android/AndroidManifest.xml "${FORTIQ_ANDROID_PACKAGE_OUTPUT}/AndroidManifest.xml" COPYONLY)
    # Java side of the remembered-secret store (Android Keystore AES-GCM).
    configure_file(android/src/fr/cybou/fortiq/SecretVault.java
        "${FORTIQ_ANDROID_PACKAGE_OUTPUT}/src/fr/cybou/fortiq/SecretVault.java" COPYONLY)
    configure_file(android/res/xml/network_security_config.xml "${FORTIQ_ANDROID_PACKAGE_OUTPUT}/res/xml/network_security_config.xml" COPYONLY)
    if(FORTIQ_WITH_ANDROID_WEBVIEW)
        # Browser engine on the system WebView: its Java bridge, the foreground service that keeps the gateway alive while the app is
        # minimised, cleartext to the loopback gateway (network security config) and androidx.webkit (ProxyController, document-start script).
        set(FORTIQ_ANDROIDX_WEBKIT "androidx.webkit:webkit:1.15.0")
        foreach(f FortiqWeb.java GatewayService.java)
            configure_file(android/src/fr/cybou/fortiq/${f} "${FORTIQ_ANDROID_PACKAGE_OUTPUT}/src/fr/cybou/fortiq/${f}" COPYONLY)
        endforeach()
        # Qt's own Gradle template plus ONE dependency (the template is read from the kit, so a Qt update keeps its other changes).
        file(READ "${QT6_INSTALL_PREFIX}/src/android/templates/build.gradle" FORTIQ_QT_GRADLE)
        set(FORTIQ_CORE_DEP "implementation 'androidx.core:core:1.17.0'")
        string(FIND "${FORTIQ_QT_GRADLE}" "${FORTIQ_CORE_DEP}" FORTIQ_CORE_POS)
        if(FORTIQ_CORE_POS EQUAL -1)
            message(FATAL_ERROR "Qt's Android build.gradle template changed: add ${FORTIQ_ANDROIDX_WEBKIT} to the dependencies by hand (CMakeLists.txt)")
        endif()
        string(REPLACE "${FORTIQ_CORE_DEP}" "${FORTIQ_CORE_DEP}
    implementation '${FORTIQ_ANDROIDX_WEBKIT}'" FORTIQ_QT_GRADLE "${FORTIQ_QT_GRADLE}")
        file(WRITE "${FORTIQ_ANDROID_PACKAGE_OUTPUT}/build.gradle" "${FORTIQ_QT_GRADLE}")
    endif()
    set_target_properties(fortiq PROPERTIES
        QT_ANDROID_COMPILE_SDK_VERSION "android-${FORTIQ_ANDROID_SDK}"
        QT_ANDROID_TARGET_SDK_VERSION "${FORTIQ_ANDROID_SDK}"
        QT_ANDROID_MIN_SDK_VERSION "28"
        QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
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
    # Bound stalled GUI/network tests; live suites get a larger explicit budget.
    add_test(NAME fortiq_brand_assets
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tool/check_brand_assets.py"
          --output "${FORTIQ_BRAND_OUTPUT}"
          --android-output "${FORTIQ_ANDROID_PACKAGE_OUTPUT}")
    find_package(Qt6 6.9 REQUIRED COMPONENTS Test)
    qt_add_executable(fortiq_terminal_history_tests tests/TerminalHistoryTest.cpp)
    target_include_directories(fortiq_terminal_history_tests PRIVATE src)
    target_link_libraries(fortiq_terminal_history_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME fortiq_terminal_history COMMAND fortiq_terminal_history_tests -o "${CMAKE_CURRENT_BINARY_DIR}/terminal-history-results.txt,txt" -o "-,txt")
    qt_add_executable(fortiq_terminal_sequences_tests tests/TerminalSequencesTest.cpp src/terminal/TerminalView.cpp src/terminal/TerminalView.h)
    target_include_directories(fortiq_terminal_sequences_tests PRIVATE src)
    target_link_libraries(fortiq_terminal_sequences_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test fortiq_vterm)
    add_test(NAME fortiq_terminal_sequences COMMAND fortiq_terminal_sequences_tests -o "${CMAKE_CURRENT_BINARY_DIR}/terminal-sequences-results.txt,txt" -o "-,txt")
    qt_add_executable(fortiq_markdown_tests tests/MarkdownDataTest.cpp)
    target_include_directories(fortiq_markdown_tests PRIVATE src)
    target_link_libraries(fortiq_markdown_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME fortiq_markdown COMMAND fortiq_markdown_tests -o "${CMAKE_CURRENT_BINARY_DIR}/markdown-results.txt,txt" -o "-,txt")
    qt_add_executable(fortiq_disk_tests tests/DiskInfoTest.cpp)
    target_include_directories(fortiq_disk_tests PRIVATE src)
    target_link_libraries(fortiq_disk_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME fortiq_disks COMMAND fortiq_disk_tests -o "${CMAKE_CURRENT_BINARY_DIR}/disk-results.txt,txt" -o "-,txt")
    qt_add_executable(fortiq_tests tests/RemotePathTest.cpp src/core/RemotePath.cpp src/core/RemotePath.h src/core/LocalDocument.cpp src/core/LocalDocument.h)
    target_link_libraries(fortiq_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME fortiq_remote_path COMMAND fortiq_tests -o "${CMAKE_CURRENT_BINARY_DIR}/remote-path-results.txt,txt" -o "-,txt")
    # Live SSH check; skips itself unless FORTIQ_IT_* variables are set.
    qt_add_executable(fortiq_ssh_it tests/SshIntegrationTest.cpp
        src/core/TerminalController.cpp src/core/TerminalController.h
        src/core/DisksController.cpp src/core/DisksController.h
        src/core/DocumentsController.cpp src/core/DocumentsController.h
        ${FORTIQ_SSH_SOURCES} src/ssh/SshClient.h src/core/RemotePath.cpp src/core/RemotePath.h
        src/core/LocalDocument.cpp src/core/LocalDocument.h
        ${FORTIQ_REMOTE_SOURCES} src/core/RemoteController.h src/core/SecretStore.cpp src/core/SecretStore.h src/core/TaskManager.cpp src/core/TaskManager.h
        ${FORTIQ_LOGS_SOURCES} src/core/LogsController.h ${FORTIQ_BROWSER_SOURCES} ${FORTIQ_NETWORK_SOURCES} ${FORTIQ_SYSTEM_SOURCES} ${FORTIQ_DB_SOURCES} ${FORTIQ_MAIL_SOURCES})
    target_include_directories(fortiq_ssh_it PRIVATE src)
    target_link_libraries(fortiq_ssh_it PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent Qt6::Test ${FORTIQ_SSH_TARGET})
    add_test(NAME fortiq_ssh_integration COMMAND fortiq_ssh_it -o "${CMAKE_CURRENT_BINARY_DIR}/ssh-results.txt,txt" -o "-,txt")
    # Performance baseline (docs/PERFORMANCE_BASELINE.md): measures and prints, never part of ctest; run through tool/run_perf_baseline.py.
    qt_add_executable(fortiq_perf tests/PerfBaseline.cpp src/terminal/TerminalView.cpp src/terminal/TerminalView.h
        src/core/TerminalController.cpp src/core/TerminalController.h
        src/core/DisksController.cpp src/core/DisksController.h
        src/core/DocumentsController.cpp src/core/DocumentsController.h
        ${FORTIQ_SSH_SOURCES} src/ssh/SshClient.h src/core/RemotePath.cpp src/core/RemotePath.h
        src/core/LocalDocument.cpp src/core/LocalDocument.h
        ${FORTIQ_REMOTE_SOURCES} src/core/RemoteController.h src/core/SecretStore.cpp src/core/SecretStore.h src/core/TaskManager.cpp src/core/TaskManager.h
        ${FORTIQ_LOGS_SOURCES} src/core/LogsController.h ${FORTIQ_BROWSER_SOURCES} ${FORTIQ_NETWORK_SOURCES} ${FORTIQ_SYSTEM_SOURCES} ${FORTIQ_DB_SOURCES} ${FORTIQ_MAIL_SOURCES})
    target_include_directories(fortiq_perf PRIVATE src)
    target_link_libraries(fortiq_perf PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Network Qt6::Concurrent ${FORTIQ_SSH_TARGET} fortiq_vterm)
    if(WIN32)
        target_link_libraries(fortiq_perf PRIVATE psapi)
    endif()
    # Scene-cost baseline of the Files window with N entries (docs/PERFORMANCE_BASELINE.md); never part of ctest.
    qt_add_executable(fortiq_perf_scene tests/PerfFilesScene.cpp)
    target_link_libraries(fortiq_perf_scene PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 fortiq_shellplugin)
    # Real screenshot of the running app on a disposable Linux server (website hero); run by tool/capture_app_screenshot.py, never shipped.
    get_target_property(FORTIQ_APP_SOURCES fortiq SOURCES)
    list(FILTER FORTIQ_APP_SOURCES EXCLUDE REGEX "src/main\.cpp$")
    list(FILTER FORTIQ_APP_SOURCES EXCLUDE REGEX "\.rc$")
    list(FILTER FORTIQ_APP_SOURCES EXCLUDE REGEX "(qmltyperegistrations|_qml_Main_qml|Main_qml\.cpp|qrc_|_autogen|mocs_compilation|\.rcc/)")
    qt_add_executable(fortiq_live_capture tests/LiveCapture.cpp ${FORTIQ_APP_SOURCES})
    target_include_directories(fortiq_live_capture PRIVATE src)
    qt_add_qml_module(fortiq_live_capture ${FORTIQ_QML_SCAN_OPTIONS} URI Fortiq.App VERSION 1.0
        IMPORT_PATH ${CMAKE_CURRENT_BINARY_DIR}/qml
        OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/capture_qml/Fortiq/App QML_FILES qml/Main.qml)
    qt_add_resources(fortiq_live_capture "capture_icon" PREFIX "/branding" BASE "${FORTIQ_BRAND_OUTPUT}"
        FILES "${FORTIQ_BRAND_OUTPUT}/fortiq_mark.png")
    target_link_libraries(fortiq_live_capture PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Concurrent fortiq_shellplugin ${FORTIQ_SSH_TARGET} fortiq_vterm)
    qt_add_executable(fortiq_logs_tests tests/LogsControllerTest.cpp
        ${FORTIQ_SSH_SOURCES} src/ssh/SshClient.h src/core/RemotePath.cpp src/core/RemotePath.h
        src/core/LocalDocument.cpp src/core/LocalDocument.h
        ${FORTIQ_REMOTE_SOURCES} src/core/RemoteController.h src/core/SecretStore.cpp src/core/SecretStore.h src/core/TaskManager.cpp src/core/TaskManager.h
        ${FORTIQ_LOGS_SOURCES} src/core/LogsController.h)
    target_include_directories(fortiq_logs_tests PRIVATE src)
    target_link_libraries(fortiq_logs_tests PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent Qt6::Test ${FORTIQ_SSH_TARGET})
    add_test(NAME fortiq_logs COMMAND fortiq_logs_tests -o "${CMAKE_CURRENT_BINARY_DIR}/logs-results.txt,txt" -o "-,txt")
    qt_add_executable(fortiq_credentials_tests tests/CredentialsTest.cpp
        ${FORTIQ_SSH_SOURCES} src/ssh/SshClient.h src/core/RemotePath.cpp src/core/RemotePath.h
        src/core/LocalDocument.cpp src/core/LocalDocument.h
        ${FORTIQ_REMOTE_SOURCES} src/core/RemoteController.h src/core/SecretStore.cpp src/core/SecretStore.h src/core/TaskManager.cpp src/core/TaskManager.h)
    target_include_directories(fortiq_credentials_tests PRIVATE src)
    target_link_libraries(fortiq_credentials_tests PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent Qt6::Test ${FORTIQ_SSH_TARGET})
    add_test(NAME fortiq_credentials COMMAND fortiq_credentials_tests -o "${CMAKE_CURRENT_BINARY_DIR}/credentials-results.txt,txt" -o "-,txt")
    qt_add_executable(fortiq_publication_tests tests/PublicationTest.cpp
        ${FORTIQ_SSH_SOURCES} src/ssh/SshClient.h src/core/RemotePath.cpp src/core/RemotePath.h src/core/LocalDocument.cpp src/core/LocalDocument.h)
    target_compile_definitions(fortiq_publication_tests PRIVATE FORTIQ_PUBLICATION_TESTS)
    target_include_directories(fortiq_publication_tests PRIVATE src tests)
    target_link_libraries(fortiq_publication_tests PRIVATE Qt6::Core Qt6::Network Qt6::Test ${FORTIQ_SSH_TARGET})
    add_test(NAME fortiq_publication COMMAND fortiq_publication_tests -o "${CMAKE_CURRENT_BINARY_DIR}/publication-results.txt,txt" -o "-,txt")
    qt_add_executable(fortiq_task_tests tests/TaskManagerTest.cpp src/core/TaskManager.cpp src/core/TaskManager.h)
    target_include_directories(fortiq_task_tests PRIVATE src)
    target_link_libraries(fortiq_task_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME fortiq_tasks COMMAND fortiq_task_tests -o "${CMAKE_CURRENT_BINARY_DIR}/task-results.txt,txt" -o "-,txt")
    qt_add_executable(fortiq_browser_tests tests/BrowserTest.cpp ${FORTIQ_BROWSER_PURE_SOURCES})
    target_include_directories(fortiq_browser_tests PRIVATE src)
    target_link_libraries(fortiq_browser_tests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    add_test(NAME fortiq_browser COMMAND fortiq_browser_tests -o "${CMAKE_CURRENT_BINARY_DIR}/browser-results.txt,txt" -o "-,txt")
    qt_add_executable(fortiq_gateway_tests tests/GatewayTest.cpp ${FORTIQ_GATEWAY_PURE_SOURCES})
    target_include_directories(fortiq_gateway_tests PRIVATE src)
    target_compile_definitions(fortiq_gateway_tests PRIVATE FORTIQ_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(fortiq_gateway_tests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    add_test(NAME fortiq_gateway COMMAND fortiq_gateway_tests -o "${CMAKE_CURRENT_BINARY_DIR}/gateway-results.txt,txt" -o "-,txt")
    # Live check of the gateway + SSH tunnel against the disposable fixture (skips itself unless FORTIQ_IT_* are set; tool/run_live_tests.py runs it).
    qt_add_executable(fortiq_gateway_it tests/GatewayLiveTest.cpp ${FORTIQ_GATEWAY_PURE_SOURCES} ${FORTIQ_GATEWAY_TUNNEL_SOURCES} src/ssh/SshClient.h)
    target_include_directories(fortiq_gateway_it PRIVATE src)
    target_compile_definitions(fortiq_gateway_it PRIVATE FORTIQ_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(fortiq_gateway_it PRIVATE Qt6::Core Qt6::Network Qt6::Test ${FORTIQ_SSH_TARGET})
    if(WIN32)
        target_link_libraries(fortiq_gateway_it PRIVATE iphlpapi ws2_32)
    endif()
    add_test(NAME fortiq_gateway_live COMMAND fortiq_gateway_it -o "${CMAKE_CURRENT_BINARY_DIR}/gateway-live-results.txt,txt" -o "-,txt")
    # Engine rules + EngineBrowser lifecycle (no Qt WebEngine, no SSH): switch audit, navigation policy, credentials, gateway lifecycle.
    qt_add_executable(fortiq_engine_tests tests/EngineProfileTest.cpp ${FORTIQ_ENGINE_SOURCES} ${FORTIQ_GATEWAY_PURE_SOURCES})
    target_include_directories(fortiq_engine_tests PRIVATE src)
    target_compile_definitions(fortiq_engine_tests PRIVATE FORTIQ_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(fortiq_engine_tests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    add_test(NAME fortiq_engine COMMAND fortiq_engine_tests -o "${CMAKE_CURRENT_BINARY_DIR}/engine-results.txt,txt" -o "-,txt")
    if(FORTIQ_WITH_WEBENGINE)
        # The REAL Chromium + gateway + SSH tunnel against the disposable fixture: JavaScript through the gateway, and the leak test
        # (socket tables of the whole process tree). Skips itself unless FORTIQ_IT_* are set; tool/run_live_tests.py runs it.
        qt_add_executable(fortiq_engine_it tests/EngineLiveTest.cpp ${FORTIQ_ENGINE_SOURCES} ${FORTIQ_GATEWAY_PURE_SOURCES} ${FORTIQ_GATEWAY_TUNNEL_SOURCES} src/ssh/SshClient.h)
        target_include_directories(fortiq_engine_it PRIVATE src)
        target_compile_definitions(fortiq_engine_it PRIVATE FORTIQ_HAVE_WEBENGINE FORTIQ_ENGINE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/engine"
            FORTIQ_QT_QML_DIR="$<TARGET_FILE_DIR:Qt6::Core>/../qml"
            FORTIQ_QT_PREFIX="$<TARGET_FILE_DIR:Qt6::Core>/..")
        target_link_libraries(fortiq_engine_it PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Network Qt6::Test Qt6::WebEngineQuick
            fortiq_engineplugin fortiq_shellplugin ${FORTIQ_SSH_TARGET})
        if(WIN32)
            target_link_libraries(fortiq_engine_it PRIVATE iphlpapi ws2_32)
        endif()
        add_test(NAME fortiq_engine_live COMMAND fortiq_engine_it -o "${CMAKE_CURRENT_BINARY_DIR}/engine-live-results.txt,txt" -o "-,txt")
    endif()
    qt_add_executable(fortiq_network_tests tests/NetworkTest.cpp ${FORTIQ_NETWORK_PURE_SOURCES})
    target_include_directories(fortiq_network_tests PRIVATE src)
    target_compile_definitions(fortiq_network_tests PRIVATE FORTIQ_NET_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/network")
    target_link_libraries(fortiq_network_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME fortiq_network COMMAND fortiq_network_tests -o "${CMAKE_CURRENT_BINARY_DIR}/network-results.txt,txt" -o "-,txt")
    qt_add_executable(fortiq_system_tests tests/SystemTest.cpp ${FORTIQ_SYSTEM_PURE_SOURCES})
    target_include_directories(fortiq_system_tests PRIVATE src)
    target_compile_definitions(fortiq_system_tests PRIVATE FORTIQ_SYS_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/system")
    target_link_libraries(fortiq_system_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME fortiq_system COMMAND fortiq_system_tests -o "${CMAKE_CURRENT_BINARY_DIR}/system-results.txt,txt" -o "-,txt")
    qt_add_executable(fortiq_db_tests tests/DbTest.cpp ${FORTIQ_DB_PURE_SOURCES})
    target_include_directories(fortiq_db_tests PRIVATE src)
    target_link_libraries(fortiq_db_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME fortiq_db COMMAND fortiq_db_tests -o "${CMAKE_CURRENT_BINARY_DIR}/db-results.txt,txt" -o "-,txt")
    qt_add_executable(fortiq_mail_tests tests/MailTest.cpp ${FORTIQ_MAIL_PURE_SOURCES} src/browser/HtmlSanitizer.cpp src/browser/HtmlSanitizer.h src/browser/UrlTools.cpp src/browser/UrlTools.h)
    target_include_directories(fortiq_mail_tests PRIVATE src)
    target_compile_definitions(fortiq_mail_tests PRIVATE FORTIQ_MAIL_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/mail")
    target_link_libraries(fortiq_mail_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME fortiq_mail COMMAND fortiq_mail_tests -o "${CMAKE_CURRENT_BINARY_DIR}/mail-results.txt,txt" -o "-,txt")
    qt_add_executable(fortiq_mailsend_tests tests/MailSendTest.cpp ${FORTIQ_MAIL_PURE_SOURCES} src/browser/HtmlSanitizer.cpp src/browser/HtmlSanitizer.h src/browser/UrlTools.cpp src/browser/UrlTools.h)
    target_include_directories(fortiq_mailsend_tests PRIVATE src)
    target_link_libraries(fortiq_mailsend_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME fortiq_mailsend COMMAND fortiq_mailsend_tests -o "${CMAKE_CURRENT_BINARY_DIR}/mailsend-results.txt,txt" -o "-,txt")
    # Fuzzing and sanitizers (audit R-backlog 5, docs/FUZZING.md). The smoke driver runs everywhere; libFuzzer and -fsanitize need clang/gcc.
    set(FORTIQ_FUZZ_SOURCES fuzz/fuzz_targets.cpp fuzz/fuzz_targets.h ${FORTIQ_BROWSER_PURE_SOURCES} ${FORTIQ_NETWORK_PURE_SOURCES} ${FORTIQ_SYSTEM_PURE_SOURCES}
        ${FORTIQ_DB_PURE_SOURCES} ${FORTIQ_MAIL_PURE_SOURCES})
    list(REMOVE_DUPLICATES FORTIQ_FUZZ_SOURCES)
    qt_add_executable(fortiq_fuzz_smoke fuzz/fuzz_smoke.cpp ${FORTIQ_FUZZ_SOURCES})
    target_include_directories(fortiq_fuzz_smoke PRIVATE src fuzz)
    target_link_libraries(fortiq_fuzz_smoke PRIVATE Qt6::Core Qt6::Network)
    add_test(NAME fortiq_fuzz_smoke COMMAND fortiq_fuzz_smoke "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/mail" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/network"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/system" "${CMAKE_CURRENT_SOURCE_DIR}/fuzz/seeds" --iterations 4000)
    if(FORTIQ_FUZZ)
        qt_add_executable(fortiq_fuzz ${FORTIQ_FUZZ_SOURCES})
        target_compile_definitions(fortiq_fuzz PRIVATE FORTIQ_LIBFUZZER)
        target_include_directories(fortiq_fuzz PRIVATE src fuzz)
        target_compile_options(fortiq_fuzz PRIVATE -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -g)
        target_link_options(fortiq_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_libraries(fortiq_fuzz PRIVATE Qt6::Core Qt6::Network)
    endif()
    qt_add_executable(fortiq_link_tests tests/LinkMonitorTest.cpp src/ssh/LinkMonitor.cpp src/ssh/LinkMonitor.h)
    target_include_directories(fortiq_link_tests PRIVATE src)
    target_link_libraries(fortiq_link_tests PRIVATE Qt6::Core Qt6::Concurrent Qt6::Test)
    add_test(NAME fortiq_link COMMAND fortiq_link_tests -o "${CMAKE_CURRENT_BINARY_DIR}/link-results.txt,txt" -o "-,txt")
    qt_add_executable(fortiq_document_tests tests/LocalDocumentTest.cpp src/core/LocalDocument.cpp src/core/LocalDocument.h)
    target_include_directories(fortiq_document_tests PRIVATE src)
    target_link_libraries(fortiq_document_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME fortiq_documents COMMAND fortiq_document_tests -o "${CMAKE_CURRENT_BINARY_DIR}/document-results.txt,txt" -o "-,txt")
    qt_add_executable(fortiq_highlighter_tests tests/MarkdownHighlighterTest.cpp src/documents/MarkdownHighlighter.cpp src/documents/MarkdownHighlighter.h)
    target_include_directories(fortiq_highlighter_tests PRIVATE src)
    target_link_libraries(fortiq_highlighter_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Test)
    add_test(NAME fortiq_highlighter COMMAND fortiq_highlighter_tests -o "${CMAKE_CURRENT_BINARY_DIR}/highlighter-results.txt,txt" -o "-,txt")
    qt_add_executable(fortiq_layout_tests tests/ShellLayoutTest.cpp src/core/TaskManager.cpp src/core/TaskManager.h)
    qt_add_qml_module(fortiq_layout_tests ${FORTIQ_QML_SCAN_OPTIONS} URI Fortiq.App VERSION 1.0
        IMPORT_PATH ${CMAKE_CURRENT_BINARY_DIR}/qml
        OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/layout_qml/Fortiq/App QML_FILES qml/Main.qml)
    qt_add_resources(fortiq_layout_tests "layout_icon" PREFIX "/branding" BASE "${FORTIQ_BRAND_OUTPUT}"
        FILES "${FORTIQ_BRAND_OUTPUT}/fortiq_mark.png")
    target_link_libraries(fortiq_layout_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Test fortiq_shellplugin)
    add_test(NAME fortiq_layout COMMAND fortiq_layout_tests -o "${CMAKE_CURRENT_BINARY_DIR}/layout-results.txt,txt" -o "-,txt")
    get_property(FORTIQ_REGISTERED_TESTS DIRECTORY PROPERTY TESTS)
    set_tests_properties(${FORTIQ_REGISTERED_TESTS} PROPERTIES TIMEOUT 120)
    set_tests_properties(fortiq_ssh_integration fortiq_publication fortiq_gateway_live PROPERTIES TIMEOUT 600)
    if(TEST fortiq_engine_live)
        set_tests_properties(fortiq_engine_live PROPERTIES TIMEOUT 600)
    endif()
    # Interactive desktop workflows render many scenes; keep a bounded budget on slower CI hosts.
    set_tests_properties(fortiq_layout PROPERTIES TIMEOUT 240)
    set(FORTIQ_LAYOUT_ENV "QT_QUICK_CONTROLS_STYLE=Fusion;QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    if(TARGET Qt6::QOffscreenIntegrationPlugin)   # Qt from the online installer; distribution packages find the plugin by themselves
        list(APPEND FORTIQ_LAYOUT_ENV "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>")
    endif()
    set_tests_properties(fortiq_layout PROPERTIES ENVIRONMENT "${FORTIQ_LAYOUT_ENV}")
    if(WIN32)
        file(TO_CMAKE_PATH "$ENV{WINDIR}/Fonts" FORTIQ_TEST_FONT_DIR)
        set_property(TEST fortiq_layout APPEND PROPERTY ENVIRONMENT "QT_QPA_FONTDIR=${FORTIQ_TEST_FONT_DIR}")
        # Developer tests need the matching Qt Test runtime, absent from the app package.
        set_tests_properties(${FORTIQ_REGISTERED_TESTS} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:${FORTIQ_SSH_TARGET}>")
    endif()
endif()

# Windows uses shared Qt and explicitly linked FORTIQ QML plugins. Packaging
# already runs windeployqt --qmldir qml (tool/package_windows.py). Automatic
# per-executable import scans traverse the entire source tree, including old
# build/package copies of Qt; they are unnecessary for this deployment route.
# Keep Qt's scanner enabled for Android, static Qt and native Linux packaging.
if(WIN32 AND QT6_IS_SHARED_LIBS_BUILD)
    get_property(FORTIQ_DIRECTORY_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(FORTIQ_TARGET IN LISTS FORTIQ_DIRECTORY_TARGETS)
        get_target_property(FORTIQ_TARGET_TYPE ${FORTIQ_TARGET} TYPE)
        if(FORTIQ_TARGET_TYPE STREQUAL "EXECUTABLE")
            set_property(TARGET ${FORTIQ_TARGET} PROPERTY QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
        endif()
    endforeach()
endif()

# Debian graphical-client package. Enabled only on native Linux builds, never
# for the Android cross build. The remote Linux host still needs no agent.
option(FORTIQ_DEB "Enable native Debian .deb packaging (client)" OFF)
if(FORTIQ_DEB)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "FORTIQ_DEB requires a native Linux target")
    endif()
    execute_process(COMMAND dpkg --print-architecture
        OUTPUT_VARIABLE FORTIQ_DEB_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE FORTIQ_DPKG_RESULT)
    if(NOT FORTIQ_DPKG_RESULT EQUAL 0 OR FORTIQ_DEB_ARCH STREQUAL "")
        message(FATAL_ERROR "dpkg is required to build a Debian package")
    endif()
    include(GNUInstallDirs)
    # Debian 13 ships Qt 6.8; the shell needs Qt 6.9+ (RectangularShadow), so the package carries its own Qt 6.12
    # under /opt/fortiq whenever the build uses a Qt that is not the system one.
    set(FORTIQ_DEB_BUNDLE_QT OFF)
    if(NOT Qt6_DIR MATCHES "^/usr/")
        set(FORTIQ_DEB_BUNDLE_QT ON)
    endif()
    if(FORTIQ_DEB_BUNDLE_QT)
        set(CMAKE_INSTALL_PREFIX "/opt/fortiq" CACHE PATH "" FORCE)
        set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/fortiq")
        install(TARGETS fortiq RUNTIME DESTINATION bin)
        qt_generate_deploy_qml_app_script(TARGET fortiq OUTPUT_SCRIPT FORTIQ_DEPLOY_SCRIPT
            NO_UNSUPPORTED_PLATFORM_ERROR NO_TRANSLATIONS)
        install(SCRIPT ${FORTIQ_DEPLOY_SCRIPT})
        set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/prerm")
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS "/opt/fortiq/lib")
    else()
        install(TARGETS fortiq RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
    install(FILES packaging/debian/fr.cybou.fortiq.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES assets/branding/fortiq_mark.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
        RENAME fr.cybou.fortiq.png)
    install(FILES LICENSE docs/THIRD_PARTY_LICENSES.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/fortiq)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "fortiq")
    set(CPACK_PACKAGE_VENDOR "CYBOU")
    set(CPACK_PACKAGE_CONTACT "FORTIQ maintainers")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "FORTIQ remote Linux desktop client over SSH")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_FILE_NAME "fortiq_${PROJECT_VERSION}_${FORTIQ_DEB_ARCH}")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${FORTIQ_DEB_ARCH}")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # ELF dependency discovery cannot see QML imports.
    if(NOT FORTIQ_DEB_BUNDLE_QT)
        set(CPACK_DEBIAN_PACKAGE_DEPENDS
            "qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-window")
    endif()
    set(CPACK_DEBIAN_PACKAGE_SECTION "net")
    include(CPack)
endif()
