cmake_minimum_required(VERSION 3.20)

project(mini_redis_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MINI_REDIS_BUILD_TESTS "Build tests" ON)
option(MINI_REDIS_BUILD_BENCHMARKS "Build benchmark client" ON)
option(MINI_REDIS_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(MINI_REDIS_BUILD_FUZZER "Build the libFuzzer RESP parser target" OFF)

if(MINI_REDIS_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    else()
        message(WARNING "Sanitizers are only configured for Clang and GCC")
    endif()
endif()

add_library(miniredis
    src/aof.cpp
    src/command.cpp
    src/config.cpp
    src/durable_executor.cpp
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

if(MINI_REDIS_BUILD_FUZZER)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_executable(miniredis_resp_fuzz tests/resp_fuzz.cpp)
        target_link_libraries(miniredis_resp_fuzz PRIVATE miniredis)
        target_compile_options(miniredis_resp_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(miniredis_resp_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
    else()
        message(FATAL_ERROR "MINI_REDIS_BUILD_FUZZER requires Clang")
    endif()
endif()

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
        tests/thread_pool_test.cpp
        tests/ttl_test.cpp
    )
    target_link_libraries(miniredis_tests PRIVATE miniredis)
    target_compile_options(miniredis_tests PRIVATE
        $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-UNDEBUG>
    )
    add_test(NAME miniredis_tests COMMAND miniredis_tests)

    add_executable(miniredis_integration_tests tests/integration_test.cpp)
    target_link_libraries(miniredis_integration_tests PRIVATE miniredis)
    target_compile_options(miniredis_integration_tests PRIVATE
        $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-UNDEBUG>
    )
    add_test(
        NAME miniredis_integration_tests
        COMMAND miniredis_integration_tests $<TARGET_FILE:miniredis_server>
    )
endif()

if(MINI_REDIS_BUILD_BENCHMARKS)
    add_executable(miniredis_benchmark benchmarks/benchmark_client.cpp)
    target_link_libraries(miniredis_benchmark PRIVATE miniredis)
endif()
