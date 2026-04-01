cmake_minimum_required(VERSION 3.25)

project(tcp_server_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TCP_SERVER_BUILD_TESTS "Build unit and integration tests" ON)
option(TCP_SERVER_BUILD_BENCHMARKS "Build benchmark targets" ON)
option(TCP_SERVER_ENABLE_CLANG_TIDY "Enable clang-tidy target" ON)

add_library(tcp_server_core STATIC
    src/core/runtime.cpp
    src/core/lifecycle.cpp
    src/net/socket.cpp
    src/net/acceptor.cpp
    src/net/event_loop.cpp
    src/net/connection.cpp
    src/protocol/frame_decoder.cpp
    src/protocol/frame_encoder.cpp
    src/app/dispatcher.cpp
    src/config/config_loader.cpp
    src/config/config_validator.cpp
    src/infra/logging.cpp
    src/infra/metrics.cpp
)

target_include_directories(tcp_server_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(tcp_server_core PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(tcp_server_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(tcp_server_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(tcp_server
    src/main.cpp
)

target_link_libraries(tcp_server PRIVATE tcp_server_core)

if(TCP_SERVER_BUILD_TESTS)
    include(FetchContent)

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4
    )
    FetchContent_MakeAvailable(Catch2)

    enable_testing()

    add_executable(unit_config_tests
        tests/unit/config_tests.cpp
    )
    target_link_libraries(unit_config_tests PRIVATE tcp_server_core Catch2::Catch2WithMain)

    add_test(NAME unit_config_tests COMMAND unit_config_tests)

    add_executable(integration_server_startup_tests
        tests/integration/server_startup_tests.cpp
    )
    target_link_libraries(integration_server_startup_tests PRIVATE tcp_server_core)
    add_test(NAME integration_server_startup_tests COMMAND integration_server_startup_tests)

    if(WIN32)
        get_filename_component(TCP_SERVER_TOOLCHAIN_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(unit_config_tests PROPERTIES
            ENVIRONMENT "PATH=${TCP_SERVER_TOOLCHAIN_BIN_DIR};$ENV{PATH}"
        )
        set_tests_properties(integration_server_startup_tests PROPERTIES
            ENVIRONMENT "PATH=${TCP_SERVER_TOOLCHAIN_BIN_DIR};$ENV{PATH}"
        )
    endif()
endif()

if(TCP_SERVER_BUILD_BENCHMARKS)
    include(FetchContent)

    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.9.0
    )
    FetchContent_MakeAvailable(benchmark)

    add_executable(benchmark_decoder
        benchmarks/decoder_benchmark.cpp
    )
    target_link_libraries(benchmark_decoder PRIVATE tcp_server_core benchmark::benchmark_main)
endif()

if(TCP_SERVER_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(TCP_SERVER_CLANG_TIDY_SOURCES
            src/main.cpp
            src/core/runtime.cpp
            src/core/lifecycle.cpp
            src/net/socket.cpp
            src/net/acceptor.cpp
            src/net/event_loop.cpp
            src/net/connection.cpp
            src/protocol/frame_decoder.cpp
            src/protocol/frame_encoder.cpp
            src/app/dispatcher.cpp
            src/infra/logging.cpp
            src/infra/metrics.cpp
        )

        add_custom_target(clang-tidy
            COMMAND
                ${CLANG_TIDY_EXE}
                --quiet
                -p ${CMAKE_BINARY_DIR}
                ${TCP_SERVER_CLANG_TIDY_SOURCES}
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            COMMENT "Running clang-tidy"
            VERBATIM
        )
    else()
        message(WARNING "clang-tidy not found; 'clang-tidy' target will not be available.")
    endif()
endif()

