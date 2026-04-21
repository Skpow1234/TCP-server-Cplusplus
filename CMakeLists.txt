cmake_minimum_required(VERSION 3.25)

project(tcp_server_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TCP_SERVER_BUILD_TESTS "Build unit and integration tests" ON)
option(TCP_SERVER_BUILD_BENCHMARKS "Build benchmark targets" ON)
option(TCP_SERVER_ENABLE_CLANG_TIDY "Enable clang-tidy target" ON)
option(TCP_SERVER_ADD_SANITIZER_DRIVER_TARGETS "Add sanitizer-*-test targets (configure/build/ctest out-of-tree sanitizer presets)" ON)

add_library(tcp_server_core STATIC
    src/core/runtime.cpp
    src/core/lifecycle.cpp
    src/net/socket.cpp
    src/net/acceptor.cpp
    src/net/event_loop.cpp
    src/net/select_poller.cpp
    src/net/accept_ops.cpp
    src/net/connection.cpp
    src/net/idle_timeout_manager.cpp
    src/net/recv.cpp
    src/net/send.cpp
    src/protocol/frame_decoder.cpp
    src/protocol/frame_encoder.cpp
    src/app/dispatcher.cpp
    src/runtime/worker_pool.cpp
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

if(WIN32)
    target_link_libraries(tcp_server_core PUBLIC ws2_32)
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
        tests/unit/frame_decoder_tests.cpp
        tests/unit/frame_encoder_tests.cpp
        tests/unit/dispatcher_tests.cpp
        tests/unit/worker_pool_tests.cpp
        tests/unit/thread_safety_stress_tests.cpp
        tests/unit/idle_timeout_manager_tests.cpp
        tests/unit/lifecycle_tests.cpp
    )
    target_link_libraries(unit_config_tests PRIVATE tcp_server_core Catch2::Catch2WithMain)

    add_test(NAME unit_config_tests COMMAND unit_config_tests)

    add_executable(integration_server_startup_tests
        tests/integration/server_startup_tests.cpp
    )
    target_link_libraries(integration_server_startup_tests PRIVATE tcp_server_core)
    add_test(NAME integration_server_startup_tests COMMAND integration_server_startup_tests)

    add_executable(integration_echo_roundtrip_tests
        tests/integration/echo_roundtrip_tests.cpp
    )
    target_link_libraries(integration_echo_roundtrip_tests PRIVATE tcp_server_core)
    add_test(NAME integration_echo_roundtrip_tests COMMAND integration_echo_roundtrip_tests)

    add_executable(integration_graceful_shutdown_tests
        tests/integration/graceful_shutdown_tests.cpp
    )
    target_link_libraries(integration_graceful_shutdown_tests PRIVATE tcp_server_core)
    add_test(NAME integration_graceful_shutdown_tests COMMAND integration_graceful_shutdown_tests)

    if(WIN32)
        get_filename_component(TCP_SERVER_TOOLCHAIN_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(unit_config_tests PROPERTIES
            ENVIRONMENT "PATH=${TCP_SERVER_TOOLCHAIN_BIN_DIR};$ENV{PATH}"
        )
        set_tests_properties(integration_server_startup_tests PROPERTIES
            ENVIRONMENT "PATH=${TCP_SERVER_TOOLCHAIN_BIN_DIR};$ENV{PATH}"
        )
        set_tests_properties(integration_echo_roundtrip_tests PROPERTIES
            ENVIRONMENT "PATH=${TCP_SERVER_TOOLCHAIN_BIN_DIR};$ENV{PATH}"
        )
        set_tests_properties(integration_graceful_shutdown_tests PROPERTIES
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

if(TCP_SERVER_ADD_SANITIZER_DRIVER_TARGETS)
    macro(tcp_server_add_sanitizer_driver target_name preset_name)
        add_custom_target(
            ${target_name}
            COMMAND ${CMAKE_COMMAND} -E echo "Sanitizer: ${preset_name} (configure + build + ctest)"
            COMMAND ${CMAKE_COMMAND} --preset ${preset_name} -S "${CMAKE_SOURCE_DIR}" -B "${CMAKE_SOURCE_DIR}/out/build/${preset_name}"
            COMMAND ${CMAKE_COMMAND} --build "${CMAKE_SOURCE_DIR}/out/build/${preset_name}" --parallel
            COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${CMAKE_SOURCE_DIR}/out/build/${preset_name}" --output-on-failure
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            USES_TERMINAL
            COMMENT "Sanitizer preset ${preset_name}: configure, build, ctest"
            VERBATIM
        )
    endmacro()

    tcp_server_add_sanitizer_driver(sanitizer-asan-ubsan-test asan-ubsan)
    tcp_server_add_sanitizer_driver(sanitizer-tsan-test tsan)
    tcp_server_add_sanitizer_driver(sanitizer-mingw-asan-ubsan-test mingw-asan-ubsan)
    tcp_server_add_sanitizer_driver(sanitizer-mingw-tsan-test mingw-tsan)
endif()

if(TCP_SERVER_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(TCP_SERVER_CLANG_TIDY_SOURCES
            src/main.cpp
            src/core/runtime.cpp
            src/core/lifecycle.cpp
            src/protocol/frame_decoder.cpp
            src/protocol/frame_encoder.cpp
            src/app/dispatcher.cpp
            src/runtime/worker_pool.cpp
            src/net/idle_timeout_manager.cpp
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

