#include "lua_helpers.h"
#include <dirent.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include "plugin_manager.h"

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

// C function exposed to Lua: plugin_register_hook("hook_name", "lua_function")
int l_register_hook(lua_State *L) {
  Plugin *p = (Plugin *)lua_touserdata(L, lua_upvalueindex(1));
  PluginManager *pm = (PluginManager *)lua_touserdata(L, lua_upvalueindex(2));

  if (pm->hook_count >= pm->hook_capacity) {
    luaL_error(L, "Hook limit reached");
    return 0;
  }

  const char *hook_name = luaL_checkstring(L, 1);
  const char *func_name = luaL_checkstring(L, 2);
  int priority = (int)luaL_optinteger(L, 3, 100);

  // 1. ALLOCATE the struct for this slot
  pm->hook_list[pm->hook_count] = malloc(sizeof(HookRegistration));
  if (!pm->hook_list[pm->hook_count]) {
      luaL_error(L, "Out of memory");
      return 0;
  }

  // 2. Assign values
  pm->hook_list[pm->hook_count]->hook_name = strdup(hook_name);
  pm->hook_list[pm->hook_count]->lua_func_name = strdup(func_name);
  pm->hook_list[pm->hook_count]->plugin = p;
  pm->hook_list[pm->hook_count]->priority = priority; // Don't forget this!
  
  pm->hook_count++;

  // 3. SAFE POINTER SWAP (Bubble Sort)
  for (int i = 0; i < pm->hook_count - 1; i++) {
    for (int j = 0; j < pm->hook_count - i - 1; j++) {
      if (pm->hook_list[j]->priority > pm->hook_list[j + 1]->priority) {
        // Swap the pointers, not the contents
        HookRegistration *temp = pm->hook_list[j];
        pm->hook_list[j] = pm->hook_list[j + 1];
        pm->hook_list[j + 1] = temp;
      }
    }
  }

  return 0;
}

// call this before running any plugin scripts
void register_logger(lua_State *L) {
  lua_pushcfunction(L, l_log);
  lua_setglobal(L, "c_log"); // Expose to Lua as a global
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
    fprintf(stderr, "Error loading internal module %s: %s\n", name,
            lua_tostring(L, -1));
    lua_pop(L, 1);
  }

  // Clean up stack (pop preload and package)
  lua_pop(L, 2);
}