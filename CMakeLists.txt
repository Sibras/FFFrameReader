# CMakeList.txt : CMake project for FFFR, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.10.2)

project(FfFrameReader
    VERSION 0.1
    DESCRIPTION "FFmpeg based library for reading frames from a video source"
    LANGUAGES CXX CUDA
)

# Use vcpkg if available
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build position independent code.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Disable C and C++ compiler specific extensions.
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export all symbols from Windows dlls into corresponding lib file
if(MSVC)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# Defines the CMAKE_INSTALL_LIBDIR, CMAKE_INSTALL_BINDIR and many other useful macros.
include(GNUInstallDirs)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")

# Differentiate between release/debug builds
if(MSVC)
    set(CMAKE_DEBUG_POSTFIX "d")
endif()

# Shared/Dynamic or Static library?
option(BUILD_SHARED_LIBS "Build libraries as shared as opposed to static" ON)

# Build test related commands?
option(BUILD_TESTING "Create test programs" OFF)

# Default to a release build if desired configuration is not specified.
if(NOT CMAKE_CONFIGURATION_TYPES)
    if(NOT CMAKE_BUILD_TYPE)
        message(STATUS "Setting build type to 'Release' as none was specified.")
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY VALUE "Release")
    endif()
endif()

# Add project source files
set(FFFR_SOURCES
    source/FFFR.cpp
    source/FFFRStream.cpp
    source/FFFRDecoderContext.cpp
    source/FFFRFilter.cpp
    source/FFFRFrame.cpp
    source/FFFREncoder.cpp
    source/FFFRUtility.cpp
    source/FFFRFormatConvert.cu
    source/FFFRTypes.cpp
    include/FFFRDecoderContext.h
    include/FFFRFilter.h
    include/FFFRUtility.h
    include/FFFRTypes.h
)

set(FFFR_HEADERS
    include/FFFrameReader.h
    include/FFFRStream.h
    include/FFFRFrame.h
    include/FFFREncoder.h
)

# Add source to this project's executable.
if(BUILD_SHARED_LIBS)
    add_library(FfFrameReader
        SHARED
        ${FFFR_SOURCES}
        ${FFFR_HEADERS}
    )
else()
    add_library(FfFrameReader
        STATIC
        ${FFFR_SOURCES}
        ${FFFR_HEADERS}
    )
endif()

set_target_properties(FfFrameReader PROPERTIES
    FRAMEWORK ON
    SOVERSION 0
    VERSION ${PROJECT_VERSION}
    PUBLIC_HEADER "${FFFR_HEADERS}"
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_RESOLVE_DEVICE_SYMBOLS ON
)

# Enable all compile warnings
if(MSVC)
  target_compile_options(FfFrameReader PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /WX /experimental:external /external:anglebrackets /external:W3>)
else()
  target_compile_options(FfFrameReader PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -pedantic -Werror>)
endif()

# Find the required FFmpeg libraries
find_path(AVCODEC_INCLUDE_DIR NAMES libavcodec/avcodec.h)
find_library(AVCODEC_LIBRARY NAMES avcodec)

find_path(AVFORMAT_INCLUDE_DIR NAMES libavformat/avformat.h)
find_library(AVFORMAT_LIBRARY NAMES avformat)

find_path(AVFILTER_INCLUDE_DIR NAMES libavfilter/avfilter.h)
find_library(AVFILTER_LIBRARY NAMES avfilter)

find_path(AVUTIL_INCLUDE_DIR NAMES libavutil/avutil.h)
find_library(AVUTIL_LIBRARY NAMES avutil)

find_library(SWSCALE_LIBRARY NAMES swscale)
find_library(POSTPROC_LIBRARY NAMES postproc)
find_library(SWRESAMPLE_LIBRARY NAMES swresample)

find_package(CUDA QUIET REQUIRED)

target_include_directories(FfFrameReader PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${AVCODEC_INCLUDE_DIR}
    ${AVFORMAT_INCLUDE_DIR}
    ${AVFILTER_INCLUDE_DIR}
    ${AVUTIL_INCLUDE_DIR}
    ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES}
)
target_link_libraries(FfFrameReader
    PRIVATE ${AVCODEC_LIBRARY}
    PRIVATE ${AVFORMAT_LIBRARY}
    PRIVATE ${AVFILTER_LIBRARY}
    PRIVATE ${AVUTIL_LIBRARY}
    PRIVATE ${SWSCALE_LIBRARY}
    PRIVATE ${POSTPROC_LIBRARY}
    PRIVATE ${SWRESAMPLE_LIBRARY}
    PRIVATE ${CUDA_CUDA_LIBRARY}
    PRIVATE ${CUDA_nppicc_LIBRARY}
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
    ARCHIVE DESTINATION lib
    PUBLIC_HEADER DESTINATION include
)

# Add tests
if(BUILD_TESTING)
    enable_testing()
    find_package(GTest REQUIRED)
    include(GoogleTest)

    set(FFFR_TEST_SOURCES
        test/FFFRTestStream.cpp
        test/FFFRTestDecode.cpp
        test/FFFRTestFrame.cpp
        test/FFFRTestFilter.cpp
        test/FFFRTestEncode.cpp
        test/FFFRTestData.h
    )

    add_executable(FFFRTest ${FFFR_TEST_SOURCES})

    target_include_directories(FFFRTest PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES}
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