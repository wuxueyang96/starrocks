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
    roaring::roaring)
