#ifndef LUA_HELPERS_H
#define LUA_HELPERS_H
#include "plugin_manager.h"
#include <lua.h>


int l_get_mem_usage(lua_State *L);

void copy_value(lua_State *L_from, lua_State *L_to, int idx);

// Your requested functions
void copy_table_between_states(lua_State *from, lua_State *to, int index);

void copy_value_back(lua_State *from, lua_State *to, int index);
void setup_lua_environment(lua_State *L, Plugin *p, PluginManager *pm);

#endif