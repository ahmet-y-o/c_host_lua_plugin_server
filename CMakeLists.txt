cmake_minimum_required(VERSION 3.10)
project(MyPluginServer VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Force Debug flags if not specified (Essential for Valgrind!)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -Wall -Wextra")

find_package(PkgConfig REQUIRED)
# Added sqlite3 to the required modules
pkg_check_modules(DEPS REQUIRED libmicrohttpd libcjson sqlite3) 
find_package(Lua REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(include ${LUA_INCLUDE_DIR} ${DEPS_INCLUDE_DIRS})

set(SOURCES 
    src/main.c 
    src/plugin_manager.c 
    src/server.c
    src/lua_helpers.c
)

add_executable(main.out ${SOURCES})

target_link_libraries(main.out 
    PRIVATE 
    ${LUA_LIBRARIES} 
    ${DEPS_LIBRARIES}
    Threads::Threads
    m  # Link math library for Lua
)

add_custom_target(copy_plugins
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins"
    "${CMAKE_CURRENT_BINARY_DIR}/plugins"
    COMMENT "Refreshing plugins directory..."
)
add_dependencies(main.out copy_plugins)