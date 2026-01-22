cmake_minimum_required(VERSION 3.12)
project(MQA_identifier)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT (MSVC))
    set(CMAKE_CXX_FLAGS "-O3 -march=native -Wall -ffast-math") 
else ()
    set(CMAKE_CXX_FLAGS "/O2 /std:c++17 /EHsc")
    STRING(REPLACE "/O2" "/Od" CMAKE_CXX_FLAGS_RELEASE ${CMAKE_CXX_FLAGS_RELEASE})
endif ()

# Add search paths for macOS Homebrew
if(APPLE)
    include_directories("/opt/homebrew/include" "/usr/local/include")
    link_directories("/opt/homebrew/lib" "/usr/local/lib")
endif()

# Try to find packages using find_package first (works with vcpkg and CMake)
find_package(FLAC CONFIG QUIET)
find_package(Ogg CONFIG QUIET)

# Fallback to pkg-config if find_package didn't work
if(NOT FLAC_FOUND)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(FLAC QUIET flac++)
    endif()
endif()

# Final fallback to find_library (for older system installations)
if(TARGET FLAC::FLAC++)
    # Use vcpkg/CMake target libraries
    set(FLAC_LIBRARIES FLAC::FLAC++ FLAC::FLAC Ogg::ogg)
elseif(NOT FLAC_LIBRARIES)
    find_library(FLACPP_LIB NAMES FLAC++ flac++ libFLAC++ libflac++)
    find_library(FLAC_LIB NAMES FLAC flac libFLAC libflac)
    find_library(OGG_LIB NAMES ogg libogg)

    if(NOT FLACPP_LIB OR NOT FLAC_LIB OR NOT OGG_LIB)
        message(FATAL_ERROR "FLAC/ogg libraries not found. Please install:\n  Linux: libflac++-dev libogg-dev\n  macOS: brew install flac libogg\n  Windows: vcpkg install libflac:x64-windows libogg:x64-windows")
    endif()
    
    set(FLAC_LIBRARIES ${FLACPP_LIB} ${FLAC_LIB} ${OGG_LIB})
endif()

add_executable(${PROJECT_NAME} main.cc)
target_link_libraries(${PROJECT_NAME} ${FLAC_LIBRARIES})
if(FLAC_INCLUDE_DIRS)
    target_include_directories(${PROJECT_NAME} PRIVATE ${FLAC_INCLUDE_DIRS})
endif()
