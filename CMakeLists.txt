cmake_minimum_required(VERSION 3.13)
project(sdk_benchmark)
set(CMAKE_CXX_STANDARD 20)

if(NOT DEFINED BUCKET_NAME)
    set(BUCKET_NAME "benchmark-version-test-sbiscigl" CACHE STRING "s3 bucket to use for testing")
endif()

if(NOT DEFINED FILE_LOCATION)
    set(FILE_LOCATION "test-files/100mb.txt" CACHE STRING "location of file to test")
endif()

if(NOT DEFINED SDK_VERSION)
    set(SDK_VERSION "1.11.519" CACHE STRING "sdk version")
endif()

configure_file(cmake/constants.h.in ${CMAKE_BINARY_DIR}/include/constants.h)

include(FetchContent)

FetchContent_Declare(gbench
    GIT_REPOSITORY https://github.com/google/benchmark
    GIT_TAG        v1.8.3
)
FetchContent_MakeAvailable(gbench)

set(BUILD_ONLY "s3" CACHE STRING "Build only library" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "Enable AWS SDK TEST" FORCE)
set(AUTORUN_UNIT_TESTS OFF CACHE BOOL "Enable AWS SDK UNIT TEST" FORCE)
set(AWS_SDK_WARNINGS_ARE_ERRORS OFF CACHE BOOL "treat warnings as errors" FORCE)

FetchContent_Declare(aws-sdk-cpp
    GIT_REPOSITORY https://github.com/aws/aws-sdk-cpp
    GIT_PROGRESS   TRUE
    GIT_TAG        ${SDK_VERSION}
)
FetchContent_MakeAvailable(aws-sdk-cpp)

add_executable(${PROJECT_NAME} "src/main.cpp")
target_link_libraries(${PROJECT_NAME} benchmark::benchmark aws-cpp-sdk-s3 aws-cpp-sdk-core)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/include)
