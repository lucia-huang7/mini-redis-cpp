cmake_minimum_required(VERSION 3.20)

project(mini_redis_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MINI_REDIS_BUILD_TESTS "Build tests" ON)
option(MINI_REDIS_BUILD_BENCHMARKS "Build benchmark client" ON)

add_library(miniredis
    src/aof.cpp
    src/command.cpp
    src/config.cpp
    src/resp.cpp
    src/server.cpp
    src/store.cpp
    src/thread_pool.cpp
    src/ttl.cpp
)

target_include_directories(miniredis
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_options(miniredis PRIVATE
    $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wall -Wextra -Wpedantic>
)

add_executable(miniredis_server src/main.cpp)
target_link_libraries(miniredis_server PRIVATE miniredis)

if(MINI_REDIS_BUILD_TESTS)
    enable_testing()

    add_executable(miniredis_tests
        tests/aof_test.cpp
        tests/command_test.cpp
        tests/resp_test.cpp
        tests/store_test.cpp
        tests/test_main.cpp
        tests/ttl_test.cpp
    )
    target_link_libraries(miniredis_tests PRIVATE miniredis)
    add_test(NAME miniredis_tests COMMAND miniredis_tests)
endif()

if(MINI_REDIS_BUILD_BENCHMARKS)
    add_executable(miniredis_benchmark benchmarks/benchmark_client.cpp)
    target_link_libraries(miniredis_benchmark PRIVATE miniredis)
endif()
