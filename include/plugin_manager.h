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
#endif