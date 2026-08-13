cmake_minimum_required(VERSION 3.16)
project(mindguard_runtime LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)

add_library(mindguard_runtime STATIC src/runtime.cpp)
target_include_directories(mindguard_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mindguard_runtime PUBLIC OpenSSL::Crypto)
target_compile_options(mindguard_runtime PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>
)

include(CTest)
enable_testing()
add_executable(mindguard_runtime_tests tests/runtime_tests.cpp)
target_link_libraries(mindguard_runtime_tests PRIVATE mindguard_runtime OpenSSL::Crypto)
target_compile_options(mindguard_runtime_tests PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>
)
add_test(NAME mindguard_runtime_tests COMMAND mindguard_runtime_tests)
