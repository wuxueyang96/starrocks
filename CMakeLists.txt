cmake_minimum_required(VERSION 3.16)
project(inverted_index_poc)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Arrow REQUIRED)
find_package(Parquet REQUIRED)

# Include directories
include_directories(${ARROW_INCLUDE_DIR})
include_directories(${PARQUET_INCLUDE_DIR})

# Add AWS SDK for C++
find_package(AWSSDK REQUIRED COMPONENTS s3)

# Add yaml-cpp for configuration
find_package(yaml-cpp REQUIRED)

find_package(fmt REQUIRED)

find_package(roaring REQUIRED)

# Add compression libraries
# 函数：find_or_pkg_config
# 参数：
#   PackageName      - 库名，用于 imported target 名字，如 "lz4"
#   CMakePackageName - find_package() 使用的名字，如 "lz4"
#   PkgConfigName    - pkg-config 使用的名字，如 "liblz4"
#
# Enhanced version with manual path fallback
# Usage:
#   find_or_pkg_config(lz4 lz4 liblz4)
#
# Optional user-defined paths:
#   - Set before calling: lz4_INCLUDE_DIR=/path/to/lz4/include
#   - Or: LZ4_INCLUDE_DIRS and LZ4_LIBRARIES
#
function(find_or_pkg_config PackageName CMakePackageName PkgConfigName)
    string(TOUPPER ${PackageName} UPPER_NAME)
    set(INC_VAR_NAME "${UPPER_NAME}_INCLUDE_DIRS")
    set(LIB_VAR_NAME "${UPPER_NAME}_LIBRARIES")

    message(STATUS "🔍 Finding ${PackageName}...")

    # ---- Step 1: Already found? ----
    if(TARGET ${PackageName}::${PackageName})
        message(STATUS "✅ Already found: ${PackageName}::${PackageName}")
        return()
    endif()

    # ---- Step 2: Try CMake find_package ----
    find_package(${CMakePackageName} QUIET)
    if(TARGET ${CMakePackageName}::${CMakePackageName})
        add_library(${PackageName}::${PackageName} ALIAS ${CMakePackageName}::${CMakePackageName})
        message(STATUS "✅ Found ${PackageName} via CMake: ${CMakePackageName}")
        return()
    elseif(TARGET ${PackageName}::${PackageName})
        message(STATUS "✅ Found ${PackageName} via CMake (direct)")
        return()
    endif()

    # ---- Step 3: Try pkg-config ----
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PC_${PackageName} IMPORTED_TARGET QUIET ${PkgConfigName})
        if(TARGET PkgConfig::PC_${PackageName})
            add_library(${PackageName}::${PackageName} ALIAS PkgConfig::PC_${PackageName})
            message(STATUS "✅ Found ${PackageName} via pkg-config: ${PkgConfigName}")
            return()
        endif()
    endif()

    # ---- Step 4: Use user-provided paths ----
    set(USER_INCLUDE "")
    set(USER_LIBRARY "")

    if(DEFINED ${INC_VAR_NAME})
        set(USER_INCLUDE "${${INC_VAR_NAME}}")
    elseif(DEFINED ${PackageName}_INCLUDE_DIR)
        set(USER_INCLUDE "${${PackageName}_INCLUDE_DIR}")
    endif()

    if(DEFINED ${LIB_VAR_NAME})
        set(USER_LIBRARY "${${LIB_VAR_NAME}}")
    elseif(DEFINED ${PackageName}_LIBRARY)
        set(USER_LIBRARY "${${PackageName}_LIBRARY}")
    endif()

    if(USER_INCLUDE AND USER_LIBRARY)
        # 验证路径存在
        foreach(lib IN LISTS USER_LIBRARY)
            if(NOT EXISTS ${lib})
                message(FATAL_ERROR "❌ User-specified library not found: ${lib}")
            endif()
        endforeach()

        add_library(${PackageName}::${PackageName} UNKNOWN IMPORTED)
        set_target_properties(${PackageName}::${PackageName} PROPERTIES
                IMPORTED_LOCATION "${USER_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${USER_INCLUDE}"
        )
        message(STATUS "✅ Found ${PackageName} via user-defined paths")
        return()
    endif()

    # ---- Step 5: Auto-discover in system paths ----
    message(STATUS "⚠️  Falling back to system path search for ${PackageName}")

    # 查找头文件：比如 lz4.h, snappy-c.h, zstd.h
    find_path(${UPPER_NAME}_INCLUDE_DIR
            NAMES ${PackageName}.h
            PATHS
            /usr/include
            /usr/local/include
            /opt/local/include
            /usr/local/homebrew/include  # macOS Homebrew Intel
            /opt/homebrew/include        # macOS Homebrew Apple Silicon
            DOC "Path to ${PackageName} headers"
    )

    # 查找库文件
    find_library(${UPPER_NAME}_LIBRARY
            NAMES ${PackageName}
            PATHS
            /usr/lib
            /usr/lib64
            /usr/local/lib
            /opt/local/lib
            /usr/local/homebrew/lib
            /opt/homebrew/lib
            DOC "Path to ${PackageName} library"
    )

    # 检查是否都找到
    if(${UPPER_NAME}_INCLUDE_DIR AND ${UPPER_NAME}_LIBRARY)
        add_library(${PackageName}::${PackageName} UNKNOWN IMPORTED)
        set_target_properties(${PackageName}::${PackageName} PROPERTIES
                IMPORTED_LOCATION "${${UPPER_NAME}_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${${UPPER_NAME}_INCLUDE_DIR}"
        )
        message(STATUS "✅ Found ${PackageName} in system paths:")
        message(STATUS "   Include: ${${UPPER_NAME}_INCLUDE_DIR}")
        message(STATUS "   Library: ${${UPPER_NAME}_LIBRARY}")
        return()
    else()
        message(WARNING "❌ Could NOT find ${PackageName}")
        message(WARNING "   Include: ${${UPPER_NAME}_INCLUDE_DIR}")
        message(WARNING "   Library: ${${UPPER_NAME}_LIBRARY}")
        message(FATAL_ERROR
                "Please install ${PackageName} via package manager or set:\n"
                "  -D${INC_VAR_NAME}=/path/to/${PackageName}/include\n"
                "  -D${LIB_VAR_NAME}=/path/to/${PackageName}/lib/lib${PackageName}.so"
        )
    endif()

endfunction()

find_or_pkg_config(lz4 lz4 liblz4)
find_or_pkg_config(snappy snappy libsnappy)
find_or_pkg_config(zstd zstd libzstd)

include_directories(include)
FILE(GLOB SRC_FILES src/*.cpp)

add_executable(inverted_index_poc main.cpp ${SRC_FILES})

# Link libraries
target_link_libraries(inverted_index_poc
    Arrow::arrow_shared
    Parquet::parquet_shared
    ${AWSSDK_LINK_LIBRARIES}
    yaml-cpp
    fmt
    roaring::roaring
    lz4::lz4
    snappy::snappy
    zstd::libzstd_shared)

add_executable(string_dedup_to_file string_dedup_to_file.cpp)
