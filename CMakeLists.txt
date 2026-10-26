cmake_minimum_required(VERSION 3.21)
project(Fortiq VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Concurrent)
file(GLOB FORTIQ_VTERM_SOURCES CONFIGURE_DEPENDS third_party/libvterm/src/*.c)
add_library(fortiq_vterm STATIC ${FORTIQ_VTERM_SOURCES})
target_include_directories(fortiq_vterm PUBLIC third_party/libvterm/include)
set_target_properties(fortiq_vterm PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
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
option(FORTIQ_FUZZ "Build the libFuzzer target (clang, with ASan/UBSan)" OFF)
option(FORTIQ_SANITIZE "Build everything with AddressSanitizer + UndefinedBehaviorSanitizer (clang/gcc)" OFF)
option(FORTIQ_SHELL_PREVIEW_ONLY "Build only the shell preview (no SSH)" OFF)
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
qt_add_qml_module(fortiq_shell
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
qt_add_qml_module(fortiq_shell_preview
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
set(FORTIQ_BROWSER_SOURCES ${FORTIQ_BROWSER_PURE_SOURCES} src/browser/BrowserController.cpp src/browser/BrowserController.h)

set(FORTIQ_NETWORK_PURE_SOURCES src/network/NetworkInfo.cpp src/network/NetworkInfo.h)
set(FORTIQ_NETWORK_SOURCES ${FORTIQ_NETWORK_PURE_SOURCES} src/network/NetworkController.cpp src/network/NetworkController.h)
set(FORTIQ_SYSTEM_PURE_SOURCES src/system/SystemInfo.cpp src/system/SystemInfo.h)
set(FORTIQ_SYSTEM_SOURCES ${FORTIQ_SYSTEM_PURE_SOURCES} src/system/SystemController.cpp src/system/SystemController.h)
set(FORTIQ_MAIL_PURE_SOURCES src/mail/MailInfo.cpp src/mail/MailInfo.h src/mail/MailSend.cpp src/mail/MailSend.h src/mail/MailAccount.cpp src/mail/MailAccount.h src/mail/MailAttachment.cpp src/mail/MailAttachment.h)
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
endif()

qt_add_qml_module(fortiq
    URI Fortiq.App
    VERSION 1.0
    IMPORT_PATH ${CMAKE_CURRENT_BINARY_DIR}/qml
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
    add_test(NAME fortiq_brand_assets
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tool/check_brand_assets.py"
          --output "${FORTIQ_BRAND_OUTPUT}"
          --android-output "${FORTIQ_ANDROID_PACKAGE_OUTPUT}")
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
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
    qt_add_qml_module(fortiq_layout_tests URI Fortiq.App VERSION 1.0
        IMPORT_PATH ${CMAKE_CURRENT_BINARY_DIR}/qml
        OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/layout_qml/Fortiq/App QML_FILES qml/Main.qml)
    qt_add_resources(fortiq_layout_tests "layout_icon" PREFIX "/branding" BASE "${FORTIQ_BRAND_OUTPUT}"
        FILES "${FORTIQ_BRAND_OUTPUT}/fortiq_mark.png")
    target_link_libraries(fortiq_layout_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Test fortiq_shellplugin)
    add_test(NAME fortiq_layout COMMAND fortiq_layout_tests -o "${CMAKE_CURRENT_BINARY_DIR}/layout-results.txt,txt" -o "-,txt")
    set(FORTIQ_LAYOUT_ENV "QT_QUICK_CONTROLS_STYLE=Fusion;QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    if(TARGET Qt6::QOffscreenIntegrationPlugin)   # Qt from the online installer; distribution packages find the plugin by themselves
        list(APPEND FORTIQ_LAYOUT_ENV "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>")
    endif()
    set_tests_properties(fortiq_layout PROPERTIES ENVIRONMENT "${FORTIQ_LAYOUT_ENV}")
    if(WIN32)
        file(TO_CMAKE_PATH "$ENV{WINDIR}/Fonts" FORTIQ_TEST_FONT_DIR)
        set_property(TEST fortiq_layout APPEND PROPERTY ENVIRONMENT "QT_QPA_FONTDIR=${FORTIQ_TEST_FONT_DIR}")
        # Developer tests need the matching Qt Test runtime, absent from the app package.
        set_tests_properties(fortiq_markdown fortiq_link fortiq_disks fortiq_terminal_history fortiq_terminal_sequences fortiq_remote_path fortiq_ssh_integration fortiq_logs fortiq_credentials fortiq_tasks fortiq_browser fortiq_network fortiq_system fortiq_db fortiq_mail fortiq_mailsend fortiq_publication fortiq_documents fortiq_highlighter fortiq_layout PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:${FORTIQ_SSH_TARGET}>")
    endif()
endif()
