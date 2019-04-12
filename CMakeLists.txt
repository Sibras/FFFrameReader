# CMakeList.txt : CMake project for FFFR, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.11)

project(FfFrameReader
    VERSION 0.1
    DESCRIPTION "FFmpeg based library for reading frames from a video source"
)

# Use vcpkg if available
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Export all symbold from Windows dlls into corresponding lib fiel
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# Defines the CMAKE_INSTALL_LIBDIR, CMAKE_INSTALL_BINDIR and many other useful macros.
include(GNUInstallDirs)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")

# Differentiate between release/debug builds 
if(MSVC)
    set(CMAKE_DEBUG_POSTFIX "d")
endif()

# Build position independent code.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Disable C and C++ compiler specific extensions.
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

# Shared/Dynamic or Static library?
option(BUILD_SHARED_LIBS "Build libraries as shared as opposed to static" ON)

# Build test related commands?
option(BUILD_TESTING "Create tests using CMake" ON)

# Default to a release build if desired configuration is not specified.
if(NOT CMAKE_CONFIGURATION_TYPES)
    if(NOT CMAKE_BUILD_TYPE)
        message(STATUS "Setting build type to 'Release' as none was specified.")
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY VALUE "Release")
    endif()
endif()

# Add project source files
set(SOURCES
      source/FFFR.cpp
      source/FFFRStream.cpp
      source/FFFRDecoderContext.cpp
      source/FFFRFrame.cpp
)

set(HEADERS
    include/FfFrameReader.h
    include/FFFRStream.h
    include/FFFRDecoderContext.h
    include/FFFRFrame.h
)

# Add source to this project's executable.
if(BUILD_SHARED_LIBS)
    add_library(FfFrameReader
        SHARED
        ${SOURCES}
        ${HEADERS}
    )
else()
    add_library(FfFrameReader
        STATIC
        ${SOURCES}
        ${HEADERS}
    )
endif()

set_target_properties(FfFrameReader PROPERTIES
    FRAMEWORK ON
    SOVERSION 0
    VERSION ${PROJECT_VERSION} 
    PUBLIC_HEADER "${HEADERS}"
)

# Enable all compile warnings
if(MSVC)
  target_compile_options(FfFrameReader PRIVATE /W4 /WX /experimental:external /external:anglebrackets /external:W3)
else()
  target_compile_options(FfFrameReader PRIVATE -Wall -Wextra -pedantic -Werror)
endif()

# Find the required FFmpeg libraries
find_path(AVCODEC_INCLUDE_DIR NAMES libavcodec/avcodec.h)
find_library(AVCODEC_LIBRARY NAMES avcodec)

find_path(AVFORMAT_INCLUDE_DIR NAMES libavformat/avformat.h)
find_library(AVFORMAT_LIBRARY NAMES avformat)

find_path(AVUTIL_INCLUDE_DIR NAMES libavutil/avutil.h)
find_library(AVUTIL_LIBRARY NAMES avutil)

target_include_directories(FfFrameReader PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${AVCODEC_INCLUDE_DIR}
    ${AVFORMAT_INCLUDE_DIR}
    ${AVUTIL_INCLUDE_DIR}
)
target_link_libraries(FfFrameReader
    PRIVATE ${AVCODEC_LIBRARY}
    PRIVATE ${AVFORMAT_LIBRARY}
    PRIVATE ${AVUTIL_LIBRARY}
)

if("${CMAKE_INSTALL_PREFIX}" STREQUAL "")
message("Installing into source folder")
# Temp set the install location to the source location
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install CACHE PATH "..." FORCE)
endif()

# Install targets.
install(TARGETS FfFrameReader
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    PUBLIC_HEADER DESTINATION include
)

# Add tests
if(BUILD_TESTING)
    enable_testing()
    find_package(GTest REQUIRED)
    include(GoogleTest)

    set(TEST_SOURCES
        test/FFFRTestStream.cpp
        test/FFFRTestDecode.cpp
        test/FFFRTestNVDec.cpp
    )

    add_executable(FFFRTest ${TEST_SOURCES})

    target_include_directories(FFFRTest PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    )

    target_link_libraries(
        FFFRTest
        PRIVATE FfFrameReader
        PRIVATE GTest::GTest
        PRIVATE GTest::Main
    )

    set_target_properties(
        FFFRTest PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/test"
    )

    add_dependencies(FFFRTest FfFrameReader)

    gtest_discover_tests(FFFRTest
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/test"
    )
endif()