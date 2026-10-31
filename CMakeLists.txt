cmake_minimum_required(VERSION 3.28)
project(AI-Phone-Calls-for-Polycom-IP-Phone VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT MINGW OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "AI Phone Calls for Polycom IP Phone supports Windows x64 with the MSYS2 UCRT64 toolchain.")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(AIPHONE_DEPS "" CACHE PATH "Verified dependency source cache")
if(NOT EXISTS "${AIPHONE_DEPS}/dr_wav.h")
    message(FATAL_ERROR "Run scripts/Bootstrap.ps1 and scripts/Build.ps1 first.")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(PJ REQUIRED libpjproject=2.17)
add_library(aiphone_core src/config.cpp src/audio.cpp src/platform.cpp)
target_include_directories(aiphone_core PUBLIC src)
target_include_directories(aiphone_core SYSTEM PUBLIC
    "${AIPHONE_DEPS}/json-3.12.0/single_include"
    "${AIPHONE_DEPS}")
target_compile_definitions(aiphone_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_compile_options(aiphone_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(aiphone_core PUBLIC ws2_32 advapi32 ole32)

add_executable(aiphone src/main.cpp src/caller.cpp)
target_include_directories(aiphone SYSTEM PRIVATE
    "${AIPHONE_DEPS}/CLI11-2.5.0/include" "${AIPHONE_DEPS}/pj-install/include")
target_compile_options(aiphone PRIVATE -Wall -Wextra -Wpedantic ${PJ_CFLAGS_OTHER})
target_link_directories(aiphone PRIVATE "${AIPHONE_DEPS}/pj-install/lib")
target_link_libraries(aiphone PRIVATE aiphone_core ${PJ_STATIC_LIBRARIES})
# Only Windows system DLLs remain; the end user does not need MSYS2 or GCC.
target_link_options(aiphone PRIVATE -static)

include(CTest)
if(BUILD_TESTING)
    set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${AIPHONE_DEPS}/Catch2-3.8.1" "${CMAKE_BINARY_DIR}/catch2" EXCLUDE_FROM_ALL)
    add_executable(aiphone_tests tests/core_tests.cpp)
    target_link_libraries(aiphone_tests PRIVATE aiphone_core Catch2::Catch2WithMain)
    target_link_options(aiphone_tests PRIVATE -static)
    add_test(NAME core COMMAND aiphone_tests)
    add_executable(aiphone_test_peer tests/sip_peer.cpp)
    target_include_directories(aiphone_test_peer SYSTEM PRIVATE
        "${AIPHONE_DEPS}/CLI11-2.5.0/include" "${AIPHONE_DEPS}/pj-install/include")
    target_compile_options(aiphone_test_peer PRIVATE ${PJ_CFLAGS_OTHER})
    target_link_directories(aiphone_test_peer PRIVATE "${AIPHONE_DEPS}/pj-install/lib")
    target_link_libraries(aiphone_test_peer PRIVATE aiphone_core ${PJ_STATIC_LIBRARIES})
    target_link_options(aiphone_test_peer PRIVATE -static)
endif()

install(TARGETS aiphone RUNTIME DESTINATION .)
