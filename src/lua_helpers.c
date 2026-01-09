#include "lua_helpers.h"
#include <dirent.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

int l_get_mem_usage(lua_State *L) {
  int kbytes = lua_gc(L, LUA_GCCOUNT, 0);
  lua_pushnumber(L, kbytes); // Return value in KB
  return 1;
}

void copy_value(lua_State *L_from, lua_State *L_to, int idx) {
    // 1. Convert relative index to absolute index immediately
    if (idx < 0 && idx > LUA_REGISTRYINDEX) {
        idx = lua_gettop(L_from) + idx + 1;
    }

    int type = lua_type(L_from, idx);
    switch (type) {
        case LUA_TSTRING:
            lua_pushstring(L_to, lua_tostring(L_from, idx));
            break;
        case LUA_TNUMBER:
            lua_pushnumber(L_to, lua_tonumber(L_from, idx));
            break;
        case LUA_TBOOLEAN:
            lua_pushboolean(L_to, lua_toboolean(L_from, idx));
            break;
        case LUA_TTABLE:
            lua_newtable(L_to);
            int target_table_idx = lua_gettop(L_to); // Store where the new table is!

            lua_pushnil(L_from); 
            while (lua_next(L_from, idx) != 0) {
                // Copy Key (at -2)
                copy_value(L_from, L_to, -2);
                // Copy Value (at -1)
                copy_value(L_from, L_to, -1);

                // Use the absolute index of the table we created
                lua_settable(L_to, target_table_idx);
                
                lua_pop(L_from, 1); // Pop value, keep key for lua_next
            }
            break;
        default:
            // Instead of nil, maybe push a string describing the skipped type
            // to help you debug why it's empty.
            lua_pushstring(L_to, "[unsupported type]"); 
            break;
    }
}

// Your requested functions
void copy_table_between_states(lua_State *from, lua_State *to, int index) {
  copy_value(from, to, index);
}

void copy_value_back(lua_State *from, lua_State *to, int index) {
  copy_value(from, to, index);
}