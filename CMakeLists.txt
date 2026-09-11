cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build, for example a CMake preset.")
endif()

option(GAMESERVER_BUILD_SERVER "Build the ServerEngine host" ON)
option(GAMESERVER_BUILD_TESTS "Build domain/content/protocol tests" ON)
option(GAMESERVER_BUILD_LEGACY "Build HUNR Java compatibility modules" ON)
if(GAMESERVER_BUILD_SERVER OR GAMESERVER_BUILD_LEGACY)
    # Root owns host + engine dependencies. Override the previous submodule-only
    # manifest in an existing build directory as well.
    set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Dependency manifest" FORCE)
endif()
project(GameServer VERSION 0.2.0 LANGUAGES C CXX)
include(cmake/CompilerOptions.cmake)

add_library(GameDomain STATIC src/content/Content.cpp src/game/World.cpp src/legacy/SkillRules.cpp)
target_include_directories(GameDomain PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
gameserver_target_defaults(GameDomain)

add_library(GameProtocol STATIC src/net/DebugProtocol.cpp)
target_link_libraries(GameProtocol PUBLIC GameDomain)
gameserver_target_defaults(GameProtocol)

if(GAMESERVER_BUILD_LEGACY)
    find_package(nlohmann_json CONFIG REQUIRED)
    find_package(OpenSSL 3.0 REQUIRED)
    find_package(unofficial-sqlite3 CONFIG QUIET)
    if(TARGET unofficial::sqlite3::sqlite3)
        set(GAMESERVER_SQLITE_TARGET unofficial::sqlite3::sqlite3)
    else()
        find_package(SQLite3 REQUIRED)
        set(GAMESERVER_SQLITE_TARGET SQLite::SQLite3)
    endif()
    add_library(GameLegacy STATIC
        src/legacy/Packet.cpp src/legacy/LegacyCodec.cpp
        src/legacy/MatrixChallenge.cpp src/legacy/EccChallenge.cpp
        src/legacy/LegacyContent.cpp src/legacy/LegacyCaches.cpp
        src/legacy/CombatRules.cpp src/legacy/LegacyWorld.cpp src/legacy/MapGeometry.cpp
        src/legacy/Identity.cpp src/legacy/SqliteIdentityStore.cpp
        src/legacy/LegacyConfiguration.cpp src/legacy/LegacyApplication.cpp)
    target_link_libraries(GameLegacy PUBLIC GameDomain nlohmann_json::nlohmann_json
        PRIVATE OpenSSL::Crypto ${GAMESERVER_SQLITE_TARGET})
    gameserver_target_defaults(GameLegacy)
endif()

if(GAMESERVER_BUILD_SERVER)
    set(SE_BUILD_DLL ON CACHE BOOL "GameServer requires the C ABI DLL" FORCE)
    set(SE_BUILD_GAME_EXAMPLE OFF CACHE BOOL "Do not build the engine's game sample")
    set(SE_BUILD_WEB_EXAMPLE OFF CACHE BOOL "Do not build the engine's web sample")
    set(BUILD_TESTING OFF) # Do not register the submodule's tests in this host project.
    add_subdirectory(external/ServerEngine EXCLUDE_FROM_ALL)

    add_executable(GameServer src/main.cpp src/net/WorldConnection.cpp)
    target_link_libraries(GameServer PRIVATE GameProtocol ServerEngineDll)
    if(GAMESERVER_BUILD_LEGACY)
        target_sources(GameServer PRIVATE src/net/StreamHost.cpp)
        target_link_libraries(GameServer PRIVATE GameLegacy)
        target_compile_definitions(GameServer PRIVATE GAMESERVER_HAS_LEGACY=1)
    endif()
    gameserver_target_defaults(GameServer)
    set_target_properties(GameServer PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    # Keep editable definitions beside the binary for command-line launches.
    add_custom_command(TARGET GameServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:GameServer>/content"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/content/demo.game" "$<TARGET_FILE_DIR:GameServer>/content/demo.game"
        VERBATIM)
    if(WIN32)
        add_custom_command(TARGET GameServer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:ServerEngineDll>" "$<TARGET_FILE_DIR:GameServer>"
            VERBATIM)
    endif()
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT GameServer)
endif()

if(GAMESERVER_BUILD_TESTS)
    enable_testing()
    add_executable(GameDomainTests tests/TestMain.cpp tests/WorldTests.cpp
        tests/ContentTests.cpp tests/ProtocolTests.cpp tests/LegacySkillTests.cpp)
    target_link_libraries(GameDomainTests PRIVATE GameProtocol)
    gameserver_target_defaults(GameDomainTests)
    add_test(NAME GameDomainTests COMMAND GameDomainTests "${CMAKE_CURRENT_SOURCE_DIR}/content/demo.game")
    if(GAMESERVER_BUILD_LEGACY)
        add_executable(GameLegacyTests tests/LegacyTestMain.cpp tests/LegacyCodecTests.cpp
            tests/MatrixChallengeTests.cpp tests/EccChallengeTests.cpp
            tests/LegacyContentTests.cpp tests/LegacyCacheTests.cpp
            tests/LegacyCombatTests.cpp tests/LegacyWorldTests.cpp tests/MapGeometryTests.cpp
            tests/IdentityTests.cpp tests/LegacyApplicationTests.cpp)
        target_link_libraries(GameLegacyTests PRIVATE GameLegacy)
        target_compile_definitions(GameLegacyTests PRIVATE
            GAME_HUNR_CONTENT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/content/hunr/content.json")
        gameserver_target_defaults(GameLegacyTests)
        add_test(NAME GameLegacyTests COMMAND GameLegacyTests)
    endif()
endif()
