#ifndef LUA_HELPERS_H
#define LUA_HELPERS_H
#include "cJSON.h"
#include "plugin_manager.h"
#include <lua.h>


int l_get_mem_usage(lua_State *L);

void copy_value(lua_State *L_from, lua_State *L_to, int idx);

// Your requested functions
void copy_table_between_states(lua_State *from, lua_State *to, int index);

void copy_value_back(lua_State *from, lua_State *to, int index);
void setup_lua_environment(lua_State *L, Plugin *p, PluginManager *pm);
cJSON *lua_table_to_json(lua_State *L, int index);
void apply_plugin_schema(lua_State *L, Plugin *p);
int l_db_exec(lua_State *L);
int l_db_query(lua_State *L);
#endif