cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build, for example a CMake preset.")
endif()

option(GAMESERVER_BUILD_SERVER "Build the ServerEngine host" ON)
option(GAMESERVER_BUILD_TESTS "Build domain/content/protocol tests" ON)
if(GAMESERVER_BUILD_SERVER)
    # Root owns host + engine dependencies. Override the previous submodule-only
    # manifest in an existing build directory as well.
    set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Dependency manifest" FORCE)
endif()
if(SE_WITH_REDIS)
    list(APPEND VCPKG_MANIFEST_FEATURES redis)
endif()
if(SE_WITH_GNS OR (SE_WITH_GAME_TRANSPORT AND NOT DEFINED SE_WITH_GNS))
    list(APPEND VCPKG_MANIFEST_FEATURES gns)
endif()
list(REMOVE_DUPLICATES VCPKG_MANIFEST_FEATURES)
project(GameServer VERSION 0.2.0 LANGUAGES C CXX)
include(cmake/CompilerOptions.cmake)

add_library(GameDomain STATIC src/content/Content.cpp src/game/World.cpp)
target_include_directories(GameDomain PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
gameserver_target_defaults(GameDomain)

add_library(GameProtocol STATIC src/net/DebugProtocol.cpp)
target_link_libraries(GameProtocol PUBLIC GameDomain)
gameserver_target_defaults(GameProtocol)

if(GAMESERVER_BUILD_SERVER)
    set(SE_BUILD_DLL ON CACHE BOOL "GameServer requires the C ABI DLL" FORCE)
    set(SE_BUILD_GAME_EXAMPLE OFF CACHE BOOL "Do not build the engine's game sample")
    set(SE_BUILD_WEB_EXAMPLE OFF CACHE BOOL "Do not build the engine's web sample")
    set(BUILD_TESTING OFF) # Do not register the submodule's tests in this host project.
    add_subdirectory(external/ServerEngine EXCLUDE_FROM_ALL)

    add_executable(GameServer src/main.cpp src/net/WorldConnection.cpp)
    target_link_libraries(GameServer PRIVATE GameProtocol ServerEngineDll)
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
        tests/ContentTests.cpp tests/ProtocolTests.cpp)
    target_link_libraries(GameDomainTests PRIVATE GameProtocol)
    gameserver_target_defaults(GameDomainTests)
    add_test(NAME GameDomainTests COMMAND GameDomainTests "${CMAKE_CURRENT_SOURCE_DIR}/content/demo.game")
endif()
