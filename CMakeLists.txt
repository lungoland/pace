cmake_minimum_required(VERSION 3.22)

project(pace VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PACE_ENABLE_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
option(PACE_BUILD_TESTS "Build unit tests" ON)
option(PACE_STATIC_GNU_RUNTIME "Statically link libstdc++ and libgcc when using GNU compilers" OFF)
option(PACE_ENABLE_STATIC_LINKING "Enable fully static linking (all libraries)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

find_package(PahoMqttCpp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

set(PACE_CORE_COMMON_SOURCES
    src/common/Pace.cpp
    src/common/Config.cpp
    src/common/MqttService.cpp
    src/common/BaseCommand.cpp
    src/common/BaseSensor.cpp
    src/common/SensorTimer.cpp
    src/common/SystemExecutor.cpp
)

if(WIN32)
    set(PACE_CORE_PLATFORM_SOURCES
        src/platform/windows/SensorProvider.cpp
        src/platform/windows/SystemExecutor.cpp
        src/platform/windows/SignalShutdownWatcher.cpp
    )
elseif(UNIX)
    set(PACE_CORE_PLATFORM_SOURCES
        src/platform/linux/SensorProvider.cpp
        src/platform/linux/SystemExecutor.cpp
        src/platform/linux/SignalShutdownWatcher.cpp
    )
else()
    message(FATAL_ERROR "Unsupported platform: this project currently supports only Linux and Windows")
endif()

add_library(pace_core
    ${PACE_CORE_COMMON_SOURCES}
    ${PACE_CORE_PLATFORM_SOURCES}
)

add_library(pace::core ALIAS pace_core)

target_include_directories(pace_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(pace_core
    PUBLIC
        PahoMqttCpp::paho-mqttpp3
        nlohmann_json::nlohmann_json
        fmt::fmt
        spdlog::spdlog
)

add_executable(pace src/common/main.cpp)
target_link_libraries(pace PRIVATE pace::core)

if(PACE_ENABLE_STATIC_LINKING AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Force static linking of all libraries for portable builds
    target_link_options(pace PRIVATE -static -static-libstdc++ -static-libgcc)
elseif(PACE_STATIC_GNU_RUNTIME AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(pace PRIVATE -static-libstdc++ -static-libgcc)
endif()

if(PACE_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        find_package(Catch2 CONFIG REQUIRED)
        set(PACE_TEST_SOURCES
            tests/ConfigTests.cpp
            tests/QueueAwaiterTests.cpp
            tests/TaskTests.cpp
            tests/TaskQueueTests.cpp
        )
        add_executable(pace_tests ${PACE_TEST_SOURCES})
        target_link_libraries(pace_tests PRIVATE pace::core Catch2::Catch2WithMain)
        if(PACE_ENABLE_STATIC_LINKING AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_link_options(pace_tests PRIVATE -static -static-libstdc++ -static-libgcc)
        elseif(PACE_STATIC_GNU_RUNTIME AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_link_options(pace_tests PRIVATE -static-libstdc++ -static-libgcc)
        endif()
        include(Catch)
        catch_discover_tests(pace_tests)
    endif()
endif()

foreach(pace_target IN ITEMS pace_core pace pace_tests)
    if(TARGET ${pace_target})
        set_project_warnings(${pace_target} ${PACE_ENABLE_WARNINGS_AS_ERRORS})
    endif()
endforeach()
