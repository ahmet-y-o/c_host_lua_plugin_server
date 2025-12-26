cmake_minimum_required(VERSION 3.10)
project(MyPluginServer VERSION 1.0 LANGUAGES C)

# 1. Specify the C Standard
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 2. Find Dependencies
# This looks for the development headers and libraries on your system
find_package(PkgConfig REQUIRED)
pkg_check_modules(MICROHTTPD REQUIRED libmicrohttpd)
find_package(Lua REQUIRED)

# 3. Include Directories
include_directories(include ${LUA_INCLUDE_DIR} ${MICROHTTPD_INCLUDE_DIRS})

# 4. Define the Executable and Source Files
set(SOURCES 
    src/main.c 
    src/plugin_manager.c 
    src/server.c
)

add_executable(main.out ${SOURCES})

# 5. Link Libraries
target_link_libraries(main.out 
    PRIVATE 
    ${LUA_LIBRARIES} 
    ${MICROHTTPD_LIBRARIES}
)

# 6.1. Define a custom target that performs the copy
add_custom_target(copy_plugins
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins"
        "${CMAKE_CURRENT_BINARY_DIR}/plugins"
    COMMENT "Refreshing plugins directory..."
)

# 6.2. Make your main executable depend on this target
# This ensures that whenever you build main.out, copy_plugins is checked/run
add_dependencies(main.out copy_plugins)