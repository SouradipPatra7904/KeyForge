cmake_minimum_required(VERSION 3.16)
project(KeyForge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable AUTOMOC for Qt integration (future GUI work)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Include public headers
include_directories(${PROJECT_SOURCE_DIR}/includes_this)

# Gather sources
file(GLOB_RECURSE SOURCES src/*.cpp)

add_executable(keyforge ${SOURCES})

# Unit tests
enable_testing()
add_subdirectory(tests)
