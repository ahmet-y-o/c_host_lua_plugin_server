#include "lua_helpers.h"
#include "applua_src.h"
#include "cJSON.h"
#include "etlua_src.h"
#include "plugin_manager.h"
#include <dirent.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <pthread.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

// returns value in KB
int l_get_mem_usage(lua_State *L) {
  int kbytes = lua_gc(L, LUA_GCCOUNT, 0);
  lua_pushnumber(L, kbytes); 
  return 1;
}

void copy_value(lua_State *L_from, lua_State *L_to, int idx) {
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
    int target_table_idx = lua_gettop(L_to);

    lua_pushnil(L_from);
    while (lua_next(L_from, idx) != 0) {
      copy_value(L_from, L_to, -2);
      copy_value(L_from, L_to, -1);
      lua_settable(L_to, target_table_idx);

      lua_pop(L_from, 1);
    }
    break;
  default:
    lua_pushstring(L_to, "[unsupported type]");
    break;
  }
}

void copy_table_between_states(lua_State *from, lua_State *to, int index) {
  copy_value(from, to, index);
}

void copy_value_back(lua_State *from, lua_State *to, int index) {
  copy_value(from, to, index);
}

void sort_hooks(PluginManager *pm) {
  if (pm->hook_count < 2)
    return;

  for (int i = 0; i < pm->hook_count - 1; i++) {
    for (int j = 0; j < pm->hook_count - i - 1; j++) {
      if (pm->hook_list[j]->priority > pm->hook_list[j + 1]->priority) {
        // swap pointers
        HookRegistration *temp = pm->hook_list[j];
        pm->hook_list[j] = pm->hook_list[j + 1];
        pm->hook_list[j + 1] = temp;
      }
    }
  }
}

int l_register_hook(lua_State *L) {
  Plugin *p = (Plugin *)lua_touserdata(L, lua_upvalueindex(1));
  PluginManager *pm = (PluginManager *)lua_touserdata(L, lua_upvalueindex(2));

  // get arguments BEFORE locking
  const char *hook_name = luaL_checkstring(L, 1);
  const char *func_name = luaL_checkstring(L, 2);
  int priority = (int)luaL_optinteger(L, 3, 100);

  pthread_mutex_lock(&pm->lock);

  // check for existing hooks
  for (int i = 0; i < pm->hook_count; i++) {
    if (pm->hook_list[i]->plugin == p &&
        strcmp(pm->hook_list[i]->hook_name, hook_name) == 0) {

      free(pm->hook_list[i]->lua_func_name);
      pm->hook_list[i]->lua_func_name = strdup(func_name);
      pm->hook_list[i]->priority = priority;

      sort_hooks(pm);
      pthread_mutex_unlock(&pm->lock);
      return 0;
    }
  }

  // handle capacity limit
  // TODO: increase capacity here
  if (pm->hook_count >= pm->hook_capacity) {
    pthread_mutex_unlock(&pm->lock);
    return luaL_error(L, "Hook limit reached (%d)", pm->hook_capacity);
  }

  // allocate and assign
  HookRegistration *hr = malloc(sizeof(HookRegistration));
  if (!hr) {
    pthread_mutex_unlock(&pm->lock);
    return luaL_error(L, "Out of memory");
  }

  hr->hook_name = strdup(hook_name);
  hr->lua_func_name = strdup(func_name);
  hr->plugin = p;
  hr->priority = priority;

  pm->hook_list[pm->hook_count] = hr;
  pm->hook_count++;

  sort_hooks(pm);

  pthread_mutex_unlock(&pm->lock);
  return 0;
}

// call register_logger before running any plugin scripts
void register_logger(lua_State *L) {
  lua_pushcfunction(L, l_log);
  lua_setglobal(L, "c_log"); // Expose to Lua as a global
}

void apply_plugin_schema(lua_State *L, Plugin *p) {
  // check if 'schema' table exists in the plugin's Lua state
  lua_getglobal(L, "schema");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    // no schema found, skip DB init
    return; 
  }

  char db_path[1024];
  snprintf(db_path, sizeof(db_path), "%s/plugin.db", p->path);
  sqlite3 *db;
  if (sqlite3_open(db_path, &db) != SQLITE_OK) {
    lua_pop(L, 1);
    return;
  }

  // iterate through the schema table: { table_name = { col = "type" } }
  lua_pushnil(L);
  while (lua_next(L, -2) != 0) {
    const char *table_name = lua_tostring(L, -2);

    if (lua_istable(L, -1)) {
      char sql[2048];
      snprintf(sql, sizeof(sql), "CREATE TABLE IF NOT EXISTS %s (", table_name);

      lua_pushnil(L);
      int first = 1;
      while (lua_next(L, -2) != 0) {
        const char *col_name = lua_tostring(L, -2);
        const char *col_def = lua_tostring(L, -1);

        if (!first)
          strncat(sql, ", ", sizeof(sql) - strlen(sql) - 1);
        strncat(sql, col_name, sizeof(sql) - strlen(sql) - 1);
        strncat(sql, " ", sizeof(sql) - strlen(sql) - 1);
        strncat(sql, col_def, sizeof(sql) - strlen(sql) - 1);

        first = 0;
        lua_pop(L, 1);
      }
      strncat(sql, ");", sizeof(sql) - strlen(sql) - 1);

      char *err_msg = NULL;
      if (sqlite3_exec(db, sql, NULL, NULL, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "Migration error in %s: %s\n", p->name, err_msg);
        sqlite3_free(err_msg);
      }
    }
    lua_pop(L, 1);
  }

  // close it immediately
  sqlite3_close(db); 
  // pop 'schema' table
  lua_pop(L, 1);     
}

void preload_module(lua_State *L, const char *name, const char *source) {
  // get package.preload table
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "preload");

  // load the source code
  if (luaL_loadstring(L, source) == 0) {
    lua_setfield(L, -2, name);
  } else {
    fprintf(stderr, "Error loading internal module %s: %s\n", name,
            lua_tostring(L, -1));
    lua_pop(L, 1);
  }

  // clean up stack (pop preload and package)
  lua_pop(L, 2);
}

cJSON *lua_table_to_json(lua_State *L, int index) {
  index = lua_absindex(L, index);
  cJSON *root = NULL;

  // check if table is an array or an object
  lua_pushnil(L);
  if (lua_next(L, index) != 0) {
    if (lua_isnumber(L, -2) && lua_tonumber(L, -2) == 1) {
      root = cJSON_CreateArray();
    } else {
      root = cJSON_CreateObject();
    }
    lua_pop(L, 2);
  } else {
    return cJSON_CreateObject();
  }

  lua_pushnil(L);
  while (lua_next(L, index) != 0) {
    cJSON *item = NULL;
    int type = lua_type(L, -1);

    switch (type) {
    case LUA_TNUMBER:
      item = cJSON_CreateNumber(lua_tonumber(L, -1));
      break;
    case LUA_TSTRING:
      item = cJSON_CreateString(lua_tostring(L, -1));
      break;
    case LUA_TBOOLEAN:
      item = cJSON_CreateBool(lua_toboolean(L, -1));
      break;
    case LUA_TTABLE:
      item = lua_table_to_json(L, -1);
      break;
    default:
      item = cJSON_CreateNull();
      break;
    }

    if (root->type == cJSON_Array) {
      cJSON_AddItemToArray(root, item);
    } else {
      const char *key = lua_tostring(L, -2);
      cJSON_AddItemToObject(root, key, item);
    }

    lua_pop(L, 1);
  }
  return root;
}

void setup_lua_environment(lua_State *L, Plugin *p, PluginManager *pm) {
  // 1. std libs
  luaL_openlibs(L);
  preload_module(L, "etlua", etlua_source);
  preload_module(L, "core", app_lua_source);

  // 2. get_memory function
  register_logger(L);
  lua_pushcfunction(L, l_get_mem_usage);
  lua_setglobal(L, "c_get_memory");

  // 3. register_hook function
  lua_pushlightuserdata(L, p);
  lua_pushlightuserdata(L, pm);
  lua_pushcclosure(L, l_register_hook, 2);
  lua_setglobal(L, "c_register_hook");

  // 4. call_hook function
  lua_pushlightuserdata(L, p);
  lua_pushlightuserdata(L, pm);
  lua_pushcclosure(L, l_call_hook, 2);
  lua_setglobal(L, "c_call_hook");

  // 5. enqueue_job function
  lua_pushlightuserdata(L, p);
  lua_pushlightuserdata(L, pm);
  lua_pushcclosure(L, l_enqueue_job, 2);
  lua_setglobal(L, "c_enqueue_job");

  // 6. trigger_async function
  lua_pushlightuserdata(L, p);
  lua_pushlightuserdata(L, pm);
  lua_pushcclosure(L, l_trigger_async_event, 2);
  lua_setglobal(L, "c_trigger_async_event");

  // 7. db_exec function
  lua_pushlightuserdata(L, p);
  lua_pushcclosure(L, l_db_exec, 1);
  lua_setglobal(L, "db_exec");

  // 8. db_query function
  lua_pushlightuserdata(L, p);
  lua_pushcclosure(L, l_db_query, 1);
  lua_setglobal(L, "db_query");
}

static void get_plugin_db_path(Plugin *p, char *buffer, size_t size) {
  snprintf(buffer, size, "%s/plugin.db", p->path);
}

// db_exec("INSERT INTO...")
int l_db_exec(lua_State *L) {
  Plugin *p = (Plugin *)lua_touserdata(L, lua_upvalueindex(1));
  const char *sql = luaL_checkstring(L, 1);

  char db_path[1024];
  get_plugin_db_path(p, db_path, sizeof(db_path));

  sqlite3 *db;
  if (sqlite3_open(db_path, &db) != SQLITE_OK) {
    lua_pushboolean(L, 0);
    lua_pushstring(L, sqlite3_errmsg(db));
    sqlite3_close(db);
    return 2;
  }

  char *err_msg = NULL;
  if (sqlite3_exec(db, sql, NULL, NULL, &err_msg) != SQLITE_OK) {
    lua_pushboolean(L, 0);
    lua_pushstring(L, err_msg);
    sqlite3_free(err_msg);
    sqlite3_close(db);
    return 2;
  }

  sqlite3_close(db);
  lua_pushboolean(L, 1);
  return 1;
}

// results = db_query("SELECT * FROM...")
int l_db_query(lua_State *L) {
  Plugin *p = (Plugin *)lua_touserdata(L, lua_upvalueindex(1));
  const char *sql = luaL_checkstring(L, 1);

  char db_path[1024];
  get_plugin_db_path(p, db_path, sizeof(db_path));

  sqlite3 *db;
  sqlite3_stmt *stmt;
  if (sqlite3_open(db_path, &db) != SQLITE_OK) {
    return luaL_error(L, "DB Open Error: %s", sqlite3_errmsg(db));
  }

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    const char *err = sqlite3_errmsg(db);
    sqlite3_close(db);
    return luaL_error(L, "SQL Error: %s", err);
  }
  // The main result table
  lua_newtable(L); 
  int row_idx = 1;

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    lua_pushinteger(L, row_idx++);
    // Table for this row
    lua_newtable(L); 

    int col_count = sqlite3_column_count(stmt);
    for (int i = 0; i < col_count; i++) {
      const char *col_name = sqlite3_column_name(stmt, i);
      lua_pushstring(L, col_name);

      int type = sqlite3_column_type(stmt, i);
      if (type == SQLITE_INTEGER)
        lua_pushinteger(L, sqlite3_column_int(stmt, i));
      else if (type == SQLITE_FLOAT)
        lua_pushnumber(L, sqlite3_column_double(stmt, i));
      else if (type == SQLITE_TEXT)
        lua_pushstring(L, (const char *)sqlite3_column_text(stmt, i));
      else
        lua_pushnil(L);

      lua_settable(L, -3);
    }
    lua_settable(L, -3);
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}