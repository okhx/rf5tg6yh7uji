cmake_minimum_required(VERSION 3.28)
project(mindguard_pe32plus_e2e LANGUAGES CXX)

add_subdirectory(../.. mindguard-static)
option(MINDGUARD_PE_NEGATIVE_EXECUTABLE "Exercise the DLL-only configure gate" OFF)
if(MINDGUARD_PE_NEGATIVE_EXECUTABLE)
  add_executable(mindguard_hardened_pe dll.cpp)
else()
  add_library(mindguard_hardened_pe SHARED dll.cpp)
  set_target_properties(mindguard_hardened_pe PROPERTIES PREFIX "")
endif()
mindguard_protect_cpp_target(mindguard_hardened_pe
  PROJECT_ID pe-e2e-project
  RELEASE_ID pe-e2e-release
  SOURCES dll.cpp)
