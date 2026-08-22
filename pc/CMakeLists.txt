cmake_minimum_required(VERSION 3.21)

project(grape_pc)
include(CTest)

set(GRAPE_PC ON CACHE BOOL "" FORCE)
set(GRAPE_PC_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "" FORCE)

add_subdirectory(".." "${CMAKE_CURRENT_BINARY_DIR}/grape")

include(FetchContent)
FetchContent_Declare(grape_lua
    URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
    URL_HASH SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(grape_lua)
if (NOT grape_lua_POPULATED)
    FetchContent_Populate(grape_lua)
endif()

set(LUA_SRC "${grape_lua_SOURCE_DIR}/src")
add_library(grape_lua STATIC
    ${LUA_SRC}/lapi.c ${LUA_SRC}/lauxlib.c ${LUA_SRC}/lbaselib.c
    ${LUA_SRC}/lcode.c ${LUA_SRC}/lcorolib.c ${LUA_SRC}/lctype.c
    ${LUA_SRC}/ldebug.c ${LUA_SRC}/ldo.c ${LUA_SRC}/ldump.c
    ${LUA_SRC}/lfunc.c ${LUA_SRC}/lgc.c ${LUA_SRC}/llex.c
    ${LUA_SRC}/lmathlib.c ${LUA_SRC}/lmem.c ${LUA_SRC}/lobject.c
    ${LUA_SRC}/lopcodes.c ${LUA_SRC}/lparser.c ${LUA_SRC}/lstate.c
    ${LUA_SRC}/lstring.c ${LUA_SRC}/lstrlib.c ${LUA_SRC}/ltable.c
    ${LUA_SRC}/ltablib.c ${LUA_SRC}/ltm.c ${LUA_SRC}/lundump.c
    ${LUA_SRC}/lutf8lib.c ${LUA_SRC}/lvm.c ${LUA_SRC}/lzio.c
)
target_include_directories(grape_lua PUBLIC "${LUA_SRC}")
target_sources(grape PRIVATE src/script_engine.cpp)
target_link_libraries(grape grape_lua)
