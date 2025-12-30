cmake_minimum_required(VERSION 3.12)
project(MQA_identifier)

# for android build, unhide this              
# set(Boost_USE_STATIC_LIBS OFF)
# set(Boost_USE_DEBUG_LIBS OFF)   
# set(Boost_USE_RELEASE_LIBS ON)
# set(Boost_USE_MULTITHREADED ON)
# set(Boost_USE_STATIC_RUNTIME OFF)
# find_package(Boost 1.75.0 COMPONENTS date_time filesystem system )

set(CMAKE_CXX_STANDARD 17)

if (NOT (MSVC))
    set(CMAKE_CXX_FLAGS "-O2 -fpermissive")
else (NOT (MSVC))
    set(CMAKE_CXX_FLAGS "/O2")
    STRING(REPLACE "/O2" "/Od" CMAKE_CXX_FLAGS_RELEASE ${CMAKE_CXX_FLAGS_RELEASE})
endif ()

# Find FLAC libraries
if(FLAC_INCLUDE_DIR AND FLAC_LIBRARY AND FLACPP_LIBRARY)
    # Use provided paths
    set(FLAC_INCLUDE_DIRS ${FLAC_INCLUDE_DIR})
    set(FLAC_LIBRARIES ${FLACPP_LIBRARY} ${FLAC_LIBRARY})
else()
    # Try to find using standard methods
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FLAC flac++)
    endif()
    
    if(NOT FLAC_FOUND)
        find_library(FLACPP_LIBRARIES NAMES FLAC++)
        find_library(FLAC_LIBRARIES NAMES FLAC)
        find_path(FLAC_INCLUDE_DIRS FLAC++/decoder.h)
    endif()
endif()

find_library(ogg NAMES ogg)

add_executable(${PROJECT_NAME} main.cc)

if(FLAC_INCLUDE_DIRS)
    target_include_directories(${PROJECT_NAME} PRIVATE ${FLAC_INCLUDE_DIRS})
endif()

# for android build, hide this
target_link_libraries(${PROJECT_NAME} ${FLACPP_LIBRARY} ${FLAC_LIBRARY} ${ogg})

# for android build, unhide this
# target_link_libraries(${PROJECT_NAME} FLAC++ FLAC ogg ${Boost_LIBRARIES})
