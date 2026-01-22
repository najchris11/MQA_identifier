cmake_minimum_required(VERSION 3.12)
project(MQA_identifier)

set(CMAKE_CXX_STANDARD 17)

if (NOT (MSVC))
    set(CMAKE_CXX_FLAGS "-O3 -march=native -Wall -ffast-math") 
    # Removed -static and -static-libstdc++ for macOS compatibility/warnings
else (NOT (MSVC))
    set(CMAKE_CXX_FLAGS "/O2")
    STRING(REPLACE "/O2" "/Od" CMAKE_CXX_FLAGS_RELEASE ${CMAKE_CXX_FLAGS_RELEASE})
endif ()

# Add search paths for macOS Homebrew
if(APPLE)
    include_directories("/opt/homebrew/include" "/usr/local/include")
    link_directories("/opt/homebrew/lib" "/usr/local/lib")
endif()

find_library(FLACPP_LIB NAMES FLAC++)
find_library(FLAC_LIB NAMES FLAC)
find_library(OGG_LIB NAMES ogg)

add_executable(${PROJECT_NAME} main.cc)
target_link_libraries(${PROJECT_NAME} ${FLACPP_LIB} ${FLAC_LIB} ${OGG_LIB})
