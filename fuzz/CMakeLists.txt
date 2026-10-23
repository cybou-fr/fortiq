# Standalone build of the fuzz targets for Linux with clang (libFuzzer + AddressSanitizer + UndefinedBehaviorSanitizer):
#   tool/fuzz_linux.sh does the whole job. Not part of the application build; the same sources are also built by the main
#   CMakeLists.txt as `fortiq_fuzz_smoke` (a ctest on every platform). Keep the two source lists in step.
cmake_minimum_required(VERSION 3.21)
project(fortiq_fuzz LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC OFF)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network)
set(S ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(SOURCES
    fuzz_targets.cpp
    ${S}/browser/HtmlSanitizer.cpp ${S}/browser/UrlPolicy.cpp ${S}/browser/UrlTools.cpp
    ${S}/network/NetworkInfo.cpp ${S}/system/SystemInfo.cpp ${S}/db/DbInfo.cpp
    ${S}/mail/MailInfo.cpp ${S}/mail/MailSend.cpp ${S}/mail/MailAccount.cpp ${S}/mail/MailAttachment.cpp)
set(SAN -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g -O1)

add_executable(fortiq_fuzz ${SOURCES})
target_compile_definitions(fortiq_fuzz PRIVATE FORTIQ_LIBFUZZER)
target_include_directories(fortiq_fuzz PRIVATE ${S} .)
target_compile_options(fortiq_fuzz PRIVATE ${SAN} -fsanitize=fuzzer-no-link)
target_link_options(fortiq_fuzz PRIVATE ${SAN} -fsanitize=fuzzer)
target_link_libraries(fortiq_fuzz PRIVATE Qt6::Core Qt6::Network)

# The deterministic driver under the same sanitizers (what `fortiq_fuzz_smoke` does in the main build, but with ASan/UBSan).
add_executable(fortiq_fuzz_smoke fuzz_smoke.cpp ${SOURCES})
target_include_directories(fortiq_fuzz_smoke PRIVATE ${S} .)
target_compile_options(fortiq_fuzz_smoke PRIVATE ${SAN})
target_link_options(fortiq_fuzz_smoke PRIVATE ${SAN})
target_link_libraries(fortiq_fuzz_smoke PRIVATE Qt6::Core Qt6::Network)
