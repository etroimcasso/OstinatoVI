cmake_minimum_required(VERSION 3.28)

# ---------------------------------------------------------------------------
# Release by default: a shipped artifact is never left in the toolchain's empty
# build-type state. Set before project() so it takes on single-config generators.
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type (Release by default)" FORCE)
    message(STATUS "ostinato-vi: CMAKE_BUILD_TYPE defaulted to Release.")
endif()

project(ostinato-vi
    VERSION 0.0.1
    DESCRIPTION "Final Fantasy VI — modern C++ port on the Retro++ engine"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Compiler floor (C++20; matches the engine's stated minimums). A too-old
# compiler fails at configure with a clear message, not mid-build.
# ---------------------------------------------------------------------------
set(_ostinato_id  "${CMAKE_CXX_COMPILER_ID}")
set(_ostinato_ver "${CMAKE_CXX_COMPILER_VERSION}")
if(_ostinato_id STREQUAL "GNU" AND _ostinato_ver VERSION_LESS 13)
    message(FATAL_ERROR "ostinato-vi requires GCC 13+ (found ${_ostinato_ver}).")
elseif(_ostinato_id STREQUAL "Clang" AND _ostinato_ver VERSION_LESS 16)
    message(FATAL_ERROR "ostinato-vi requires Clang 16+ (found ${_ostinato_ver}).")
elseif(_ostinato_id STREQUAL "AppleClang" AND _ostinato_ver VERSION_LESS 15)
    message(FATAL_ERROR "ostinato-vi requires AppleClang 15+ / Xcode 15+ (found ${_ostinato_ver}).")
elseif(_ostinato_id STREQUAL "MSVC" AND _ostinato_ver VERSION_LESS 19.38)
    message(FATAL_ERROR "ostinato-vi requires MSVC 19.38+ / VS 2022 17.8+ (found ${_ostinato_ver}).")
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
    set(_ostinato_warn /W4)
else()
    set(_ostinato_warn -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Port library: all port code lives here so the binary and the tests build
# against it without duplicating a main(). Links the engine PUBLIC so both
# consumers see retropp:: transitively.
# ---------------------------------------------------------------------------
add_library(ostinato-vi-lib STATIC
    src/ostinato.cpp
    src/data/attack_properties.cpp
    src/data/battle_magic_points.cpp
    src/data/character.cpp
    src/data/dance_properties.cpp
    src/data/esper_properties.cpp
    src/data/colosseum_wagers.cpp
    src/data/item_properties.cpp
    src/data/metamorph.cpp
    src/data/monster_align.cpp
    src/data/monster_attacks.cpp
    src/data/monster_items.cpp
    src/data/monster_properties.cpp
    src/data/monster_special_anim.cpp
    src/data/rng_table.cpp
    src/data/shop_properties.cpp)
# src/ is on the public include path for the data-layer headers
# ("data/character.h", "data/rng_table.h") consumed by the tests.
target_include_directories(ostinato-vi-lib PUBLIC include src)
target_compile_features(ostinato-vi-lib PUBLIC cxx_std_20)
target_compile_options(ostinato-vi-lib PRIVATE ${_ostinato_warn})
target_link_libraries(ostinato-vi-lib PUBLIC retropp::engine)

# ---------------------------------------------------------------------------
# The game binary. Currently prints identity + engine version and exits 0.
# ---------------------------------------------------------------------------
add_executable(ostinato-vi src/main.cpp)
target_compile_features(ostinato-vi PRIVATE cxx_std_20)
target_compile_options(ostinato-vi PRIVATE ${_ostinato_warn})
target_link_libraries(ostinato-vi PRIVATE ostinato-vi-lib)

# Lean shipped binary: dead-strip unreferenced code at link, strip symbols on Release.
if(APPLE)
    target_link_options(ostinato-vi PRIVATE -Wl,-dead_strip)
elseif(_ostinato_id MATCHES "GNU|Clang")
    target_compile_options(ostinato-vi PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(ostinato-vi PRIVATE -Wl,--gc-sections)
elseif(MSVC)
    target_compile_options(ostinato-vi PRIVATE /Gy)
    target_link_options(ostinato-vi PRIVATE /OPT:REF /OPT:ICF)
endif()

if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET ostinato-vi POST_BUILD
        COMMAND "${CMAKE_STRIP}" -x "$<TARGET_FILE:ostinato-vi>"
        COMMENT "ostinato-vi: stripping symbols from the Release binary"
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

add_executable(ostinato-vi-tests tests/test_smoke.cpp)
target_compile_features(ostinato-vi-tests PRIVATE cxx_std_20)
target_compile_options(ostinato-vi-tests PRIVATE ${_ostinato_warn})
target_link_libraries(ostinato-vi-tests PRIVATE ostinato-vi-lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(ostinato-vi-tests)

# Foundation-data tests: the generated enum surface + the hand-written version
# axis / packed wrappers. Own test binary so the smoke baseline stays frozen.
# tests/ is on the include path for the generated fixture.
add_executable(ostinato-vi-enum-tests tests/test_enums.cpp)
target_compile_features(ostinato-vi-enum-tests PRIVATE cxx_std_20)
target_compile_options(ostinato-vi-enum-tests PRIVATE ${_ostinato_warn})
target_include_directories(ostinato-vi-enum-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(ostinato-vi-enum-tests PRIVATE ostinato-vi-lib GTest::gtest_main)
gtest_discover_tests(ostinato-vi-enum-tests)

# Data-table tests: character base stats (char_prop) + the RNG table, each
# full-corpus tested against its generated fixture. Own binary so the
# smoke and enum baselines stay frozen.
add_executable(ostinato-vi-data-tests
    tests/test_character_base.cpp
    tests/test_rng_table.cpp)
target_compile_features(ostinato-vi-data-tests PRIVATE cxx_std_20)
target_compile_options(ostinato-vi-data-tests PRIVATE ${_ostinato_warn})
target_include_directories(ostinato-vi-data-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(ostinato-vi-data-tests PRIVATE ostinato-vi-lib GTest::gtest_main)
gtest_discover_tests(ostinato-vi-data-tests)

# Spell/esper-data tests: attack properties (magic_prop EN), per-battle
# magic points, dance attacks, esper properties, and natural magic — each
# full-corpus tested against its generated fixture. Own binary so the
# smoke, enum, and data-table baselines stay frozen. (Natural magic is a
# header-only surface — two tables, no accessor — so it adds no library
# source.)
add_executable(ostinato-vi-spell-tests
    tests/test_attack_properties.cpp
    tests/test_battle_magic_points.cpp
    tests/test_dance_properties.cpp
    tests/test_esper_properties.cpp
    tests/test_natural_magic.cpp)
target_compile_features(ostinato-vi-spell-tests PRIVATE cxx_std_20)
target_compile_options(ostinato-vi-spell-tests PRIVATE ${_ostinato_warn})
target_include_directories(ostinato-vi-spell-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(ostinato-vi-spell-tests PRIVATE ostinato-vi-lib GTest::gtest_main)
gtest_discover_tests(ostinato-vi-spell-tests)

# Item-data tests: item properties (item_prop EN), shop properties
# (shop_prop), and colosseum wagers (ColosseumProp) — each full-corpus tested
# against its generated fixture. Own binary so the smoke, enum, data-table,
# and spell/esper baselines stay frozen.
add_executable(ostinato-vi-item-tests
    tests/test_colosseum_wagers.cpp
    tests/test_item_properties.cpp
    tests/test_shop_properties.cpp)
target_compile_features(ostinato-vi-item-tests PRIVATE cxx_std_20)
target_compile_options(ostinato-vi-item-tests PRIVATE ${_ostinato_warn})
target_include_directories(ostinato-vi-item-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(ostinato-vi-item-tests PRIVATE ostinato-vi-lib GTest::gtest_main)
gtest_discover_tests(ostinato-vi-item-tests)

# Monster-data tests: monster properties (monster_prop), the metamorph
# pack + rate tables, and the six satellite tables (steal/drop items, rage,
# sketch, control, special-attack animation, vertical alignment), each
# full-corpus tested against its generated fixture. Own binary so the smoke,
# enum, data-table, spell/esper, and item baselines stay frozen.
add_executable(ostinato-vi-monster-tests
    tests/test_monster_properties.cpp
    tests/test_monster_tables.cpp)
target_compile_features(ostinato-vi-monster-tests PRIVATE cxx_std_20)
target_compile_options(ostinato-vi-monster-tests PRIVATE ${_ostinato_warn})
target_include_directories(ostinato-vi-monster-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(ostinato-vi-monster-tests PRIVATE ostinato-vi-lib GTest::gtest_main)
gtest_discover_tests(ostinato-vi-monster-tests)
