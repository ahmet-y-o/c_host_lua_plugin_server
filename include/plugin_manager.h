#ifndef PLUGIN_MANAGER_H
#define PLUGIN_MANAGER_H
#include <lua.h>      /* The core Lua VM functions */
#include <lualib.h>   /* Access to standard Lua libraries (math, io, etc) */
#include <lauxlib.h>  /* Helper functions for common tasks */

typedef struct {
    char *name;
    char *path;
    lua_State *L;
} Plugin;

typedef struct {
    Plugin **list;
    int count;
    int capacity;
} PluginManager;

PluginManager* create_manager();
void destroy_manager(PluginManager *pm);
Plugin* create_plugin(char *name, char *path);
void refresh_plugins(PluginManager *pm);
void preload_module(lua_State *L, const char *name, const char *source);
int l_log(lua_State *L);
void register_logger(lua_State *L);

typedef struct {
    char *hook_name;
    Plugin *plugin;
    char *lua_func_name; // The function name within that plugin's Lua state
} HookRegistration;

extern HookRegistration hooks[256];
extern int hook_count;

// C function exposed to Lua: plugin_register_hook("hook_name", "lua_function")
int l_register_hook(lua_State *L);

// C function exposed to Lua: plugin_call_hook("hook_name", data_table)
int l_call_hook(lua_State *L);


#endif