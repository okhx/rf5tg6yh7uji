cmake_minimum_required(VERSION 3.16)
project(mindguard_static LANGUAGES CXX)

set(MINDGUARD_PROFILE "" CACHE STRING "MindGuard profile: Dev, Hardened, or Paranoid")
set_property(CACHE MINDGUARD_PROFILE PROPERTY STRINGS Dev Hardened Paranoid)

if(NOT MINDGUARD_PROFILE)
  message(FATAL_ERROR "MINDGUARD_PROFILE is required: Dev, Hardened, or Paranoid")
elseif(MINDGUARD_PROFILE STREQUAL "Paranoid")
  message(FATAL_ERROR "MindGuard Paranoid is not implemented; refusing to downgrade the profile")
elseif(NOT MINDGUARD_PROFILE STREQUAL "Dev" AND NOT MINDGUARD_PROFILE STREQUAL "Hardened")
  message(FATAL_ERROR "Unknown MINDGUARD_PROFILE: ${MINDGUARD_PROFILE}")
endif()

if(MINDGUARD_PROFILE STREQUAL "Hardened")
  if(NOT CMAKE_SYSTEM_NAME MATCHES "Linux|Windows" OR
     NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    message(FATAL_ERROR "MindGuard Hardened currently requires Linux or Windows x86_64")
  elseif(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18)
    message(FATAL_ERROR "MindGuard Hardened requires Clang 18+")
  endif()
  set(MINDGUARD_OBFUSCATION_SEED "" CACHE STRING
      "Public 256-bit build-shape seed; empty generates a fresh value per build directory")
  if(NOT MINDGUARD_OBFUSCATION_SEED)
    string(RANDOM LENGTH 64 ALPHABET 0123456789abcdef MINDGUARD_OBFUSCATION_SEED)
    set(MINDGUARD_OBFUSCATION_SEED "${MINDGUARD_OBFUSCATION_SEED}" CACHE STRING
        "Public 256-bit build-shape seed" FORCE)
  endif()
  string(LENGTH "${MINDGUARD_OBFUSCATION_SEED}" MINDGUARD_OBFUSCATION_SEED_LENGTH)
  if(NOT MINDGUARD_OBFUSCATION_SEED_LENGTH EQUAL 64 OR
     MINDGUARD_OBFUSCATION_SEED MATCHES "[^0-9a-fA-F]")
    message(FATAL_ERROR "MINDGUARD_OBFUSCATION_SEED must be exactly 64 hexadecimal digits")
  endif()
  string(TOLOWER "${MINDGUARD_OBFUSCATION_SEED}" MINDGUARD_OBFUSCATION_SEED)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/mindguard")
  set(MINDGUARD_OBFUSCATION_MANIFEST
      "${CMAKE_CURRENT_BINARY_DIR}/mindguard/obfuscation-manifest.txt" CACHE INTERNAL "")
  file(WRITE "${MINDGUARD_OBFUSCATION_MANIFEST}"
      "format=mindguard-cpp-obfuscation-v1\nseed=${MINDGUARD_OBFUSCATION_SEED}\n")
  file(CHMOD "${MINDGUARD_OBFUSCATION_MANIFEST}"
       PERMISSIONS OWNER_READ OWNER_WRITE)
endif()

if(WIN32)
  set(MINDGUARD_RUNTIME_SOURCES src/runtime_guard_windows.cpp src/vm_signal_windows.cpp)
else()
  set(MINDGUARD_RUNTIME_SOURCES src/runtime_guard.cpp src/vm_signal.cpp)
endif()
add_library(mindguard_static STATIC src/core.cpp ${MINDGUARD_RUNTIME_SOURCES})
if(MINDGUARD_PROFILE STREQUAL "Hardened")
  set(MINDGUARD_SEAL_TOOL "" CACHE FILEPATH
      "Host MindGuard sealer required when cross-compiling")
  if(CMAKE_CROSSCOMPILING)
    if(NOT MINDGUARD_SEAL_TOOL OR NOT EXISTS "${MINDGUARD_SEAL_TOOL}")
      message(FATAL_ERROR "Cross-compiled MindGuard Hardened requires a host MINDGUARD_SEAL_TOOL")
    endif()
  else()
    if(WIN32)
      set(MINDGUARD_SEAL_TARGET mindguard_seal_pe)
      add_executable(${MINDGUARD_SEAL_TARGET} tools/seal_pe.cpp)
    else()
      set(MINDGUARD_SEAL_TARGET mindguard_seal_elf)
      add_executable(${MINDGUARD_SEAL_TARGET} tools/seal_elf.cpp)
    endif()
    set(MINDGUARD_SEAL_TARGET "${MINDGUARD_SEAL_TARGET}" CACHE INTERNAL "" FORCE)
    target_compile_features(${MINDGUARD_SEAL_TARGET} PRIVATE cxx_std_20)
    target_include_directories(${MINDGUARD_SEAL_TARGET} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  endif()
endif()
target_compile_features(mindguard_static PUBLIC cxx_std_20)
target_include_directories(mindguard_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(mindguard_static PUBLIC
  $<$<STREQUAL:${MINDGUARD_PROFILE},Dev>:MINDGUARD_PROFILE_DEV=1>
  $<$<STREQUAL:${MINDGUARD_PROFILE},Hardened>:MINDGUARD_PROFILE_HARDENED=1>
  MINDGUARD_TARGET_ID="$<TARGET_PROPERTY:NAME>"
)
if(MINDGUARD_PROFILE STREQUAL "Hardened")
  foreach(seed_index RANGE 0 3)
    math(EXPR seed_offset "${seed_index} * 16")
    string(SUBSTRING "${MINDGUARD_OBFUSCATION_SEED}" ${seed_offset} 16 seed_word)
    target_compile_definitions(mindguard_static PUBLIC
      "MINDGUARD_OBFUSCATION_SEED_${seed_index}=0x${seed_word}ULL")
  endforeach()
endif()
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(mindguard_static PUBLIC
      "/clang:-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.")
  else()
    target_compile_options(mindguard_static PUBLIC "/pathmap:${CMAKE_SOURCE_DIR}=.")
  endif()
  target_compile_options(mindguard_static PRIVATE /GL- /W4)
  if(MINDGUARD_PROFILE STREQUAL "Hardened")
    target_compile_options(mindguard_static PUBLIC /utf-8)
    target_compile_options(mindguard_static PRIVATE /Gy /Gw /Zc:inline)
  endif()
else()
  target_compile_options(mindguard_static PUBLIC
    $<$<CXX_COMPILER_ID:GNU,Clang>:-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.>
    "$<$<AND:$<STREQUAL:${MINDGUARD_PROFILE},Hardened>,$<CXX_COMPILER_ID:GNU,Clang>>:-finput-charset=UTF-8;-fexec-charset=UTF-8>")
  target_compile_options(mindguard_static PRIVATE
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-fno-lto;-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>"
    "$<$<AND:$<STREQUAL:${MINDGUARD_PROFILE},Hardened>,$<CXX_COMPILER_ID:GNU,Clang>>:-ffunction-sections;-fdata-sections;-fno-ident>"
    "$<$<AND:$<STREQUAL:${MINDGUARD_PROFILE},Hardened>,$<NOT:$<PLATFORM_ID:Windows>>,$<CXX_COMPILER_ID:GNU,Clang>>:-fvisibility=hidden;-fvisibility-inlines-hidden>")
endif()
if(WIN32)
  target_link_libraries(mindguard_static PUBLIC iphlpapi)
  if(MINGW)
    target_compile_options(mindguard_static PUBLIC -femulated-tls)
    target_link_libraries(mindguard_static PUBLIC winpthread)
  endif()
endif()

include(CTest)
if(BUILD_TESTING AND MINDGUARD_PROFILE STREQUAL "Dev")
  foreach(fixture IN ITEMS
      reject_runtime_value
      reject_pointer
      reject_long_double
      reject_indirect_macro
      reject_oversized
      reject_header_site)
    try_compile(MINDGUARD_ACCEPTED_${fixture}
      SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/${fixture}.cpp
      CMAKE_FLAGS -DCMAKE_CXX_STANDARD=20 -DCMAKE_CXX_STANDARD_REQUIRED=ON
      COMPILE_DEFINITIONS -DMINDGUARD_PROFILE_DEV=1 -DMINDGUARD_TARGET_ID="negative_fixture"
      OUTPUT_VARIABLE MINDGUARD_OUTPUT_${fixture})
    if(MINDGUARD_ACCEPTED_${fixture})
      message(FATAL_ERROR "MindGuard negative fixture unexpectedly compiled: ${fixture}")
    endif()
  endforeach()

  add_executable(mindguard_static_dev_tests tests/dev_tests.cpp)
  target_link_libraries(mindguard_static_dev_tests PRIVATE mindguard_static)
  add_test(NAME mindguard_static_dev_tests COMMAND mindguard_static_dev_tests)
  add_executable(mindguard_static_core_runner tests/core_runner.cpp)
  target_link_libraries(mindguard_static_core_runner PRIVATE mindguard_static)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MindGuardStatic.cmake")
