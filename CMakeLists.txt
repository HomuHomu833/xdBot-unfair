cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(xdBot2 VERSION 1.0.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

option(XDBOT_DISABLE_AUTO_SAFE_MODE "Disable auto safe mode at compile time" ON)
if (XDBOT_DISABLE_AUTO_SAFE_MODE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE XDBOT_DISABLE_AUTO_SAFE_MODE)
endif()

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

CPMAddPackage("gh:prevter/msgpack-for-matjson#12edd5a")
CPMAddPackage("gh:prevter/sinaps#b9c3434")
CPMAddPackage("gh:maxnut/GDR-converter#71104a9")
target_link_libraries(${PROJECT_NAME} sinaps libGDR GDRconverter)

setup_geode_mod(${PROJECT_NAME})
