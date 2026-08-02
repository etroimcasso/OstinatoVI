cmake_minimum_required(VERSION 3.28)

# ---------------------------------------------------------------------------
# Lean-build default: a shipped artifact is never left in the toolchain's empty
# build-type state. Set before project() so it takes on single-config generators.
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type (Release by default per the lean-build rule)" FORCE)
    message(STATUS "ff6-cpp: CMAKE_BUILD_TYPE defaulted to Release.")
endif()

project(ff6-cpp
    VERSION 0.0.1
    DESCRIPTION "Final Fantasy VI — modern C++ port on the Retro++ engine"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Compiler floor (C++20; matches the engine's stated minimums). A too-old
# compiler fails at configure with a clear message, not mid-build.
# ---------------------------------------------------------------------------
set(_ff6_id  "${CMAKE_CXX_COMPILER_ID}")
set(_ff6_ver "${CMAKE_CXX_COMPILER_VERSION}")
if(_ff6_id STREQUAL "GNU" AND _ff6_ver VERSION_LESS 13)
    message(FATAL_ERROR "ff6-cpp requires GCC 13+ (found ${_ff6_ver}).")
elseif(_ff6_id STREQUAL "Clang" AND _ff6_ver VERSION_LESS 16)
    message(FATAL_ERROR "ff6-cpp requires Clang 16+ (found ${_ff6_ver}).")
elseif(_ff6_id STREQUAL "AppleClang" AND _ff6_ver VERSION_LESS 15)
    message(FATAL_ERROR "ff6-cpp requires AppleClang 15+ / Xcode 15+ (found ${_ff6_ver}).")
elseif(_ff6_id STREQUAL "MSVC" AND _ff6_ver VERSION_LESS 19.38)
    message(FATAL_ERROR "ff6-cpp requires MSVC 19.38+ / VS 2022 17.8+ (found ${_ff6_ver}).")
endif()

enable_testing()

# ---------------------------------------------------------------------------
# The Retro++ engine, consumed as a source subproject. Its own tests/examples
# default OFF when it is not the top-level project, so nothing extra is fetched.
# ---------------------------------------------------------------------------
add_subdirectory(engine)

# ---------------------------------------------------------------------------
# Warning flags — applied PRIVATE to port targets only. The engine builds under
# its own warning regime. No -Werror yet.
# ---------------------------------------------------------------------------
if(MSVC)
    set(_ff6_warn /W4)
else()
    set(_ff6_warn -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Port library: all port code lives here so the binary and the tests build
# against it without duplicating a main(). Links the engine PUBLIC so both
# consumers see retropp:: transitively.
# ---------------------------------------------------------------------------
add_library(ff6-cpp-lib STATIC
    src/ostinato.cpp)
target_include_directories(ff6-cpp-lib PUBLIC include)
target_compile_features(ff6-cpp-lib PUBLIC cxx_std_20)
target_compile_options(ff6-cpp-lib PRIVATE ${_ff6_warn})
target_link_libraries(ff6-cpp-lib PUBLIC retropp::engine)

# ---------------------------------------------------------------------------
# The game binary. Phase 0: prints identity + engine version, exits 0.
# ---------------------------------------------------------------------------
add_executable(ff6-cpp src/main.cpp)
target_compile_features(ff6-cpp PRIVATE cxx_std_20)
target_compile_options(ff6-cpp PRIVATE ${_ff6_warn})
target_link_libraries(ff6-cpp PRIVATE ff6-cpp-lib)

# Lean shipped binary: dead-strip unreferenced code at link, strip symbols on Release.
if(APPLE)
    target_link_options(ff6-cpp PRIVATE -Wl,-dead_strip)
elseif(_ff6_id MATCHES "GNU|Clang")
    target_compile_options(ff6-cpp PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(ff6-cpp PRIVATE -Wl,--gc-sections)
elseif(MSVC)
    target_compile_options(ff6-cpp PRIVATE /Gy)
    target_link_options(ff6-cpp PRIVATE /OPT:REF /OPT:ICF)
endif()

if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET ff6-cpp POST_BUILD
        COMMAND "${CMAKE_STRIP}" -x "$<TARGET_FILE:ff6-cpp>"
        COMMENT "ff6-cpp: stripping symbols from the Release binary (lean-build rule)"
        VERBATIM)
endif()

# ---------------------------------------------------------------------------
# Tests: GoogleTest (fetched — the engine does not provide it to consumers).
# ---------------------------------------------------------------------------
include(FetchContent)
set(gtest_force_shared_crt ON  CACHE BOOL "" FORCE)  # match MSVC runtime
set(INSTALL_GTEST          OFF CACHE BOOL "" FORCE)  # no install pollution
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.17.0)
FetchContent_MakeAvailable(googletest)

add_executable(ff6-cpp-tests tests/test_smoke.cpp)
target_compile_features(ff6-cpp-tests PRIVATE cxx_std_20)
target_compile_options(ff6-cpp-tests PRIVATE ${_ff6_warn})
target_link_libraries(ff6-cpp-tests PRIVATE ff6-cpp-lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(ff6-cpp-tests)
