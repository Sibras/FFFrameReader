cmake_minimum_required (VERSION 3.10.2)

# Use vcpkg if available
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" ENV_VCPKG_ROOT)
    set(CMAKE_TOOLCHAIN_FILE "${ENV_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "")
endif()

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

project(FfFrameReader
    VERSION 0.1
    DESCRIPTION "FFmpeg based library for reading frames from a video source"
    LANGUAGES CXX
)

# Shared/Dynamic or Static library?
option(FFFR_BUILD_SHARED_LIBS "Build libraries as shared as opposed to static" ON)

# Build test related programs?
option(FFFR_BUILD_TESTING "Create test programs" OFF)

# Build with cuda
option(FFFR_BUILD_CUDA "Build using NVIDIA cuda support" ON)

if(FFFR_BUILD_CUDA)
    # Build with nppi library? This requires cuda 10.1 or better
    option(FFFR_BUILD_NPPI "Build using NVIDIA libnnpi format conversion" ON)
endif()

# Build benchmarking programs?
option(FFFR_BUILD_BENCHMARKING "Create benchmark programs" OFF)

# Build python bindings?
option(FFFR_BUILD_PYTHON_BINDING "Create python bindings" OFF)

# Default to a release build if desired configuration is not specified.
if(NOT CMAKE_CONFIGURATION_TYPES)
    if(NOT CMAKE_BUILD_TYPE)
        message(STATUS "Setting build type to 'Release' as none was specified.")
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY VALUE "Release")
    endif()
endif()

# Add project source files
set(FFFR_SOURCES_CUDA
    source/FFFRFormatConvert.cu
)

set(FFFR_PTX_EMBEDDED
    ${PROJECT_BINARY_DIR}/FFFREmbeddedPTX.cpp
)

set(FFFR_SOURCES_CONFIG
    ${PROJECT_BINARY_DIR}/FFFRConfig.h
)

set(FFFR_SOURCES_EXPORT
    ${PROJECT_BINARY_DIR}/FFFRExports.h
)

set(FFFR_SOURCES
    source/FFFR.cpp
    source/FFFRStream.cpp
    source/FFFRDecoderContext.cpp
    source/FFFRFilter.cpp
    source/FFFRFrame.cpp
    source/FFFREncoder.cpp
    source/FFFRUtility.cpp
    source/FFFRTypes.cpp
    source/FFFRStreamUtils.cpp
    include/FFFRDecoderContext.h
    include/FFFRFilter.h
    include/FFFRUtility.h
    include/FFFRStreamUtils.h
    ${FFFR_PTX_EMBEDDED}
    ${FFFR_SOURCES_CONFIG}
    ${FFFR_SOURCES_EXPORT}
)

set(FFFR_HEADERS
    include/FFFrameReader.h
    include/FFFRStream.h
    include/FFFRFrame.h
    include/FFFREncoder.h
    include/FFFRTypes.h
)

if(FFFR_BUILD_CUDA)
    enable_language(CUDA)
    find_package(CUDA QUIET REQUIRED)

    # Add cuda ptx generation
    add_library(FFFRPTX OBJECT ${FFFR_SOURCES_CUDA})
    set_target_properties(FFFRPTX PROPERTIES
        CUDA_PTX_COMPILATION ON
        LINKER_LANGUAGE CUDA
    )

    # Use bin2c found in cuda toolkit to embed ptx into final executable
    get_filename_component(CUDA_COMPILER_DIR "${CMAKE_CUDA_COMPILER}" DIRECTORY)
    find_program(BIN_2_C
      NAMES bin2c
      PATHS ${CUDA_COMPILER_DIR}
      )
    if(NOT BIN_2_C)
      message(FATAL_ERROR
        "bin2c not found:\n"
        "  CMAKE_CUDA_COMPILER='${CMAKE_CUDA_COMPILER}'\n"
        "  CUDA_COMPILER_DIR='${CUDA_COMPILER_DIR}'\n"
        )
    endif()

    add_custom_command(
        OUTPUT "${FFFR_PTX_EMBEDDED}"
        COMMAND ${CMAKE_COMMAND}
            "-DBIN_TO_C_COMMAND=${BIN_2_C}"
            "-DOBJECTS=$<TARGET_OBJECTS:FFFRPTX>"
            "-DOUTPUT=${FFFR_PTX_EMBEDDED}"
            -P ${PROJECT_SOURCE_DIR}/cmake/bin2c_wrapper.cmake
        VERBATIM
        DEPENDS $<TARGET_OBJECTS:FFFRPTX>
        COMMENT "Converting PTX files to a C header"
    )
else()
    # Just make a nothing file
    configure_file(include/FFFRConfig.h.in ${FFFR_PTX_EMBEDDED} @ONLY)
endif()

configure_file(include/FFFRConfig.h.in ${FFFR_SOURCES_CONFIG} @ONLY)

# Add source to this project's executable.
if(FFFR_BUILD_SHARED_LIBS)
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

target_compile_features(FfFrameReader
    PUBLIC cxx_std_17
)

include(GNUInstallDirs)
set_target_properties(FfFrameReader PROPERTIES
    FRAMEWORK ON
    SOVERSION 0
    VERSION ${PROJECT_VERSION}
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}"
)

# Detect IPO support
include(CheckIPOSupported)
check_ipo_supported(RESULT BUILD_IPO OUTPUT IPO_ERROR)
if(BUILD_IPO)
set_target_properties(FfFrameReader PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION TRUE
)
endif()

# Setup export symbol properties
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)
include(GenerateExportHeader)
generate_export_header(FfFrameReader
    EXPORT_FILE_NAME ${FFFR_SOURCES_EXPORT}
)

if(FFFR_BUILD_CUDA)
    add_dependencies(FfFrameReader FFFRPTX)
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

target_include_directories(FfFrameReader
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${AVCODEC_INCLUDE_DIR}
    PRIVATE ${AVFORMAT_INCLUDE_DIR}
    PRIVATE ${AVFILTER_INCLUDE_DIR}
    PRIVATE ${AVUTIL_INCLUDE_DIR}
    PRIVATE ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES}
    PUBLIC ${PROJECT_BINARY_DIR}
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
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Add tests
if(FFFR_BUILD_TESTING)
    enable_testing()
    find_package(GTest REQUIRED)
    include(GoogleTest)

    add_executable(FFFRTest 
        test/FFFRTestStream.cpp
        test/FFFRTestSeek.cpp
        test/FFFRTestDecode.cpp
        test/FFFRTestFrame.cpp
        test/FFFRTestFilter.cpp
        test/FFFRTestEncode.cpp
        test/FFFRTestConvert.cpp
        test/FFFRTestShared.cpp
        test/FFFRTestData.h
    )

    target_include_directories(FFFRTest PRIVATE
        ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES}
    )

    target_link_libraries(FFFRTest
        PRIVATE FfFrameReader
        PRIVATE GTest::GTest
        PRIVATE GTest::Main
        PRIVATE ${CUDA_CUDA_LIBRARY}
        PRIVATE ${AVUTIL_LIBRARY}
    )

    set_target_properties(FFFRTest PROPERTIES
        EXCLUDE_FROM_ALL true
        VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/test"
        VERSION ${PROJECT_VERSION}
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}"
    )

    # Enable all compile warnings
    if(MSVC)
        target_compile_options(FfFrameReader PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /WX /experimental:external /external:anglebrackets /external:W3>)
    else()
        target_compile_options(FfFrameReader PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -pedantic -Werror>)
    endif()

    add_dependencies(FFFRTest FfFrameReader)

    gtest_discover_tests(FFFRTest
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/test"
    )
endif()

# Add benchmarks
if(FFFR_BUILD_BENCHMARKING)
    find_package(benchmark REQUIRED)

    add_executable(FFFRBenchmark 
        benchmark/FFFRBenchmarkStream.cpp
        benchmark/FFFRBenchmarkConvert.cpp
        benchmark/FFFRBenchmarkRead.cpp
    )

    target_include_directories(FFFRBenchmark PRIVATE
        PRIVATE ${benchmark_INCLUDE_DIRS}
    )

    target_link_libraries(FFFRBenchmark
        PRIVATE FfFrameReader
        PRIVATE benchmark::benchmark
        PRIVATE benchmark::benchmark_main
    )

    set_target_properties(FFFRBenchmark PROPERTIES
        EXCLUDE_FROM_ALL true
        VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/test"
        VERSION ${PROJECT_VERSION}
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}"
    )

    if(BUILD_IPO)
        set_target_properties(FFFRBenchmark PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION TRUE
    )
    endif()

    add_dependencies(FFFRBenchmark FfFrameReader)
endif()

# Download video files used for testing and benchmarking
if(FFFR_BUILD_TESTING OR FFFR_BUILD_BENCHMARKING)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/test/data/bbb_sunflower_1080p_30fps_normal.mp4")
        file(DOWNLOAD http://distribution.bbb3d.renderfarming.net/video/mp4/bbb_sunflower_1080p_30fps_normal.mp4
            "${PROJECT_SOURCE_DIR}/test/data/bbb_sunflower_1080p_30fps_normal.mp4"
            INACTIVITY_TIMEOUT 15
            SHOW_PROGRESS
        )
    endif()
    
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/test/data/tvl_missing_pts.avi")
        file(DOWNLOAD https://cloudstor.aarnet.edu.au/plus/s/MFGSPUiEyHMi8cB/download
            "${PROJECT_SOURCE_DIR}/test/data/tvl_missing_pts.avi"
            INACTIVITY_TIMEOUT 15
            SHOW_PROGRESS
            TLS_VERIFY ON
        )
    endif()
    
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/test/data/board_game-h264.mkv")
        file(DOWNLOAD https://github.com/anibali/tvl/raw/master/data/board_game-h264.mkv
            "${PROJECT_SOURCE_DIR}/test/data/board_game-h264.mkv"
            INACTIVITY_TIMEOUT 15
            SHOW_PROGRESS
            TLS_VERIFY ON
        )
    endif()
    
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/test/data/board_game-h264-cropped.mkv")
        file(DOWNLOAD https://github.com/anibali/tvl/raw/master/data/board_game-h264-cropped.mkv
            "${PROJECT_SOURCE_DIR}/test/data/board_game-h264-cropped.mkv"
            INACTIVITY_TIMEOUT 15
            SHOW_PROGRESS
            TLS_VERIFY ON
        )
    endif()
endif()

# Add python bindings
if(FFFR_BUILD_PYTHON_BINDING)
    find_package(pybind11 REQUIRED)
    
    pybind11_add_module(pyFrameReader python/FFFRPybind11.cpp)

    configure_file(python/__init__.py.in "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}/pyFrameReader/__init__.py")

    configure_file(python/setup.py.in "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}/setup.py")

    set_target_properties(pyFrameReader PROPERTIES
        FRAMEWORK ON
        SOVERSION 0
        VERSION ${PROJECT_VERSION}
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}/pyFrameReader"
    )

    foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
        set_target_properties(pyFrameReader PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}/pyFrameReader")
    endforeach()

    target_link_libraries(pyFrameReader
        PRIVATE FfFrameReader
    )

    add_dependencies(pyFrameReader FfFrameReader)
endif()
