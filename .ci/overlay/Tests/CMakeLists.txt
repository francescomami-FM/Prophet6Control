cmake_minimum_required(VERSION 3.20)
project(Prophet6ProtocolTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(protocol_tests
    protocol_tests.cpp
    ../Source/ProtocolCore.cpp
    ../Source/ParameterMap.cpp
)
target_include_directories(protocol_tests PRIVATE ../Source)

if (MSVC)
    target_compile_options(protocol_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(protocol_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME protocol_tests COMMAND protocol_tests)

add_executable(protocol_stress_tests
    protocol_stress_tests.cpp
    ../Source/ProtocolCore.cpp
    ../Source/ParameterMap.cpp
)
target_include_directories(protocol_stress_tests PRIVATE ../Source)
if (MSVC)
    target_compile_options(protocol_stress_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(protocol_stress_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

option(P6_ENABLE_SANITIZERS "Build protocol stress tests with address and undefined behaviour sanitizers" OFF)
if (P6_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(protocol_stress_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(protocol_stress_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME protocol_stress_tests COMMAND protocol_stress_tests)
