#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include "plugin_manager.h"
#include "etlua_src.h"
#include "applua_src.h"
#include <pthread.h>
#include <time.h>

PluginManager* create_manager() {

    // 1. Allocate the manager structure itself
    PluginManager *pm = calloc(1, sizeof(PluginManager));
    if (pm == NULL) return NULL;

    // 2. Allocate the internal array of plugins
    pm->list = calloc(4, sizeof(Plugin*));
    if (pm->list == NULL) {
        free(pm); // Clean up the manager if the list fails
        return NULL;
    }

    pm->count = 0;
    pm->capacity = 4;

    return pm;
}

// destroy_plugin must only be called by PluginManager
static void destroy_plugin(Plugin *p) {
    if (p == NULL ) return;
    if (p->L) lua_close(p->L);
    free(p->name);
    free(p->path);
    free(p);
}


void destroy_manager(PluginManager *pm) {
    if (pm == NULL) return;

    // 1. Clean up each plugin (Close Lua states)
    for (int i = 0; i < pm->count; i++) {
        if (pm->list[i]) {
            destroy_plugin(pm->list[i]);
        }
    }

    // 2. Free the internal array
    free(pm->list);

    // 3. Free the manager itself
    free(pm);
}

Plugin* create_plugin(char *name, char *path) {
    // 1. allocate memory
    Plugin *p = calloc(1, sizeof(Plugin));
    if (p == NULL) return NULL;
    p->name = strdup(name);
    p->path = strdup(path);
    p->L = luaL_newstate();

    if (p->name == NULL || p->path == NULL || p->L == NULL) {
        free(p->name);
        free(p->path);
        free(p);
        return NULL;
    }

    luaL_openlibs(p->L);
    preload_module(p->L, "etlua", etlua_source);
    preload_module(p->L, "core", app_lua_source);
    register_logger(p->L);


    // 1. Load the file (compiles it to a chunk on the stack)
    size_t plugin_file_path_size = strlen(path) + 11; //  /plugin.lua is 11 characters
    char *plugin_file_path = malloc(plugin_file_path_size);
    sprintf(plugin_file_path, "%s/%s", path, "plugin.lua");
    if (luaL_loadfile(p->L, plugin_file_path) != LUA_OK) {
        printf("Syntax Error: %s\n", lua_tostring(p->L, -1));
        free(p->name);
        free(p->path);
        free(p);
        lua_close(p->L);
        free(plugin_file_path);
        return NULL;
    }

    lua_pushstring(p->L, path);
    lua_setglobal(p->L, "PLUGIN_DIR");
    char path_cmd[512];
    snprintf(path_cmd, sizeof(path_cmd), "package.path = '%s?.lua;' .. package.path", path);
    luaL_dostring(p->L, path_cmd);

    // 2. Run the chunk. 
    // This "registers" the functions/variables into the global table.
    if (lua_pcall(p->L, 0, 0, 0) != LUA_OK) {
        printf("Execution Error: %s\n", lua_tostring(p->L, -1));
        free(p->name);
        free(p->path);
        free(p);
        lua_close(p->L);
        free(plugin_file_path);
        return NULL;
    }

    // --- At this point, everything in script.lua is loaded into memory ---
    free(plugin_file_path);
    return p;
}


static bool double_capacity(PluginManager *pm) {
    int new_capacity = pm->capacity * 2;

    // use realloc the resize the array
    Plugin **new_list = realloc(pm->list, sizeof(Plugin*) * new_capacity);

    if (new_list != NULL) {
        pm->list = new_list;
        pm->capacity = new_capacity;
        return true;
    }
    return false;
}

static bool add_plugin(PluginManager *pm, Plugin *p) {
    if (pm == NULL || p == NULL) return false;

    if (pm->count >= pm->capacity) {
        if (!double_capacity(pm)) {
            return false;
        }
    }
    
    // Check if we have room (in case realloc failed) 
    // and use correct assignment syntax
    if (pm->count < pm->capacity) {
        pm->list[pm->count] = p;
        pm->count++;

        return true;
    }
    return false;
}

void refresh_plugins(PluginManager *pm) {

    // 1. Clean up each plugin (Close Lua states)

    for (int i = 0; i < pm->count; i++) {
        if (pm->list[i]) {
            destroy_plugin(pm->list[i]);
        }
    }
    pm->count = 0;


    DIR *dp;
    struct dirent *ep;
    char path_buffer[512];

    dp = opendir ("./plugins");
    if (dp != NULL) {
        while (ep = readdir(dp)) {
            if (ep->d_name[0] == '.') {
                continue;
            }
            // 2. Construct the path to the expected lua file: ./plugins/folder_name/plugin.lua
            snprintf(path_buffer, sizeof(path_buffer), "./plugins/%s/plugin.lua", ep->d_name);

            // 3. Check if the file exists and is readable
            if (access(path_buffer, R_OK) == 0) {
                // Found a valid plugin directory containing plugin.lua
                snprintf(path_buffer, sizeof(path_buffer), "./plugins/%s/", ep->d_name);
                Plugin *p = create_plugin(ep->d_name, path_buffer);
                add_plugin(pm, p);
            }
        };
        (void) closedir (dp);
        }
    else {
        perror ("Couldn't open the directory");
    }
}

void preload_module(lua_State *L, const char *name, const char *source) {
    // 1. Get package.preload table
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "preload");

    // 2. Load the source code (but don't run it yet)
    if (luaL_loadstring(L, source) == 0) {
        // 3. Set this "loader" function into preload[name]
        lua_setfield(L, -2, name);
    } else {
        fprintf(stderr, "Error loading internal module %s: %s\n", name, lua_tostring(L, -1));
        lua_pop(L, 1);
    }

    // Clean up stack (pop preload and package)
    lua_pop(L, 2);
}

#include <time.h>

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
// 1. The C function that does the actual logging
int l_log(lua_State *L) {
    const char *level = luaL_checkstring(L, 1);
    const char *msg = luaL_checkstring(L, 2);
    
    // Generate timestamp
    time_t now;
    time(&now);
    char timestamp[20];
    struct tm *tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    // --- CRITICAL SECTION START ---
    pthread_mutex_lock(&log_mutex);
    
    fprintf(stdout, "[%s] [%s] %s\n", timestamp, level, msg);
    fflush(stdout); // Force output so logs appear in real-time
    
    pthread_mutex_unlock(&log_mutex);
    // --- CRITICAL SECTION END ---
    
    return 0;
}

// 2. Modify your plugin initialization (wherever you create the lua_State)
// You need to call this before running any plugin scripts
void register_logger(lua_State *L) {
    lua_pushcfunction(L, l_log);
    lua_setglobal(L, "c_log"); // Expose to Lua as a global
}

void server_log(const char *level, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    time_t now;
    time(&now);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    pthread_mutex_lock(&log_mutex);
    printf("[%s] [%s] ", timestamp, level);
    vprintf(fmt, args);
    printf("\n");
    fflush(stdout);
    pthread_mutex_unlock(&log_mutex);

    va_end(args);
}