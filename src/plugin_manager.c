#include "plugin_manager.h"
#include "applua_src.h"
#include "etlua_src.h"
#include <dirent.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// This is the actual definition. Memory is allocated once here.
HookRegistration hooks[256];
int hook_count = 0;

PluginManager *create_manager() {

  // 1. Allocate the manager structure itself
  PluginManager *pm = calloc(1, sizeof(PluginManager));
  if (pm == NULL)
    return NULL;

  // 2. Allocate the internal array of plugins
  pm->list = calloc(4, sizeof(Plugin *));
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
  if (p == NULL)
    return;
  if (p->L)
    lua_close(p->L);
  free(p->name);
  free(p->path);
  free(p);
}

void destroy_manager(PluginManager *pm) {
  if (pm == NULL)
    return;

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

Plugin *create_plugin(char *name, char *path) {
  // 1. allocate memory
  Plugin *p = calloc(1, sizeof(Plugin));
  if (p == NULL)
    return NULL;
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
  lua_pushcfunction(p->L, l_get_mem_usage);
  lua_setglobal(p->L, "c_get_memory");

  // Register l_register_hook with 1 upvalue (the Plugin pointer)
  lua_pushlightuserdata(p->L, p); // This becomes upvalue 1
  lua_pushcclosure(p->L, l_register_hook, 1);
  lua_setglobal(p->L, "c_register_hook");

  // Register l_call_hook (l_emit_hook) - it doesn't strictly need the upvalue
  // since it iterates the global hooks array, but it's good practice.
  lua_pushlightuserdata(p->L, p);
  lua_pushcclosure(p->L, l_call_hook, 1);
  lua_setglobal(p->L, "c_call_hook");

  // 1. Load the file (compiles it to a chunk on the stack)
  size_t plugin_file_path_size =
      strlen(path) + 11; //  /plugin.lua is 11 characters
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
  snprintf(path_cmd, sizeof(path_cmd),
           "package.path = '%s?.lua;' .. package.path", path);
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
  Plugin **new_list = realloc(pm->list, sizeof(Plugin *) * new_capacity);

  if (new_list != NULL) {
    pm->list = new_list;
    pm->capacity = new_capacity;
    return true;
  }
  return false;
}

static bool add_plugin(PluginManager *pm, Plugin *p) {
  if (pm == NULL || p == NULL)
    return false;

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
  // 1. Clear existing hooks before destroying plugins
  for (int i = 0; i < hook_count; i++) {
    free(hooks[i].hook_name);
    free(hooks[i].lua_func_name);
  }
  hook_count = 0;

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

  dp = opendir("./plugins");
  if (dp != NULL) {
    while (ep = readdir(dp)) {
      if (ep->d_name[0] == '.') {
        continue;
      }
      // 2. Construct the path to the expected lua file:
      // ./plugins/folder_name/plugin.lua
      snprintf(path_buffer, sizeof(path_buffer), "./plugins/%s/plugin.lua",
               ep->d_name);

      // 3. Check if the file exists and is readable
      if (access(path_buffer, R_OK) == 0) {
        // Found a valid plugin directory containing plugin.lua
        snprintf(path_buffer, sizeof(path_buffer), "./plugins/%s/", ep->d_name);
        Plugin *p = create_plugin(ep->d_name, path_buffer);
        add_plugin(pm, p);
      }
    };
    (void)closedir(dp);
  } else {
    perror("Couldn't open the directory");
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
    fprintf(stderr, "Error loading internal module %s: %s\n", name,
            lua_tostring(L, -1));
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

// C function exposed to Lua: plugin_register_hook("hook_name", "lua_function")
int l_register_hook(lua_State *L) {

  if (hook_count >= 256) {
    luaL_error(L, "Hook limit reached (256)");
    return 0;
  }
  int priority = (int)luaL_optinteger(L, 3, 100); // Default to 100
  const char *hook_name = luaL_checkstring(L, 1);
  const char *func_name = luaL_checkstring(L, 2);

  Plugin *p = (Plugin *)lua_touserdata(L, lua_upvalueindex(1));
  if (!p) {
    luaL_error(L, "Internal Error: Plugin context lost in hook registration");
    return 0;
  }

  hooks[hook_count].hook_name = strdup(hook_name);
  hooks[hook_count].plugin = p;
  hooks[hook_count].lua_func_name = strdup(func_name);
  hook_count++;

  // Re-sort the array by priority every time a new hook is added
  // This keeps the emit loop fast and simple
  for (int i = 0; i < hook_count - 1; i++) {
    for (int j = 0; j < hook_count - i - 1; j++) {
      if (hooks[j].priority > hooks[j + 1].priority) {
        HookRegistration temp = hooks[j];
        hooks[j] = hooks[j + 1];
        hooks[j + 1] = temp;
      }
    }
  }

  return 0;
}

void monitor_plugin_memory(PluginManager *pm) {
  server_log("MONITOR", "--- Memory Usage Report ---");

  for (int i = 0; i < pm->count; i++) {
    Plugin *p = pm->list[i];

    // lua_gc with LUA_GCCOUNT returns memory in Kbytes
    int kbytes = lua_gc(p->L, LUA_GCCOUNT, 0);
    // LUA_GCCOUNTB returns the remainder in bytes
    int bytes = lua_gc(p->L, LUA_GCCOUNTB, 0);

    double total_mb = (kbytes / 1024.0) + (bytes / (1024.0 * 1024.0));

    server_log("MONITOR", "Plugin: [%s] | Usage: %.2f MB", p->name, total_mb);
  }
  server_log("MONITOR", "---------------------------");
}

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
static int call_stack_depth = 0;
#define MAX_CALL_STACK_DEPTH 10

int l_call_hook(lua_State *L) {
  if (call_stack_depth >= MAX_CALL_STACK_DEPTH) {
    return luaL_error(L,
                      "Critical Error: Max event recursion depth (%d) reached!",
                      MAX_CALL_STACK_DEPTH);
  }

  call_stack_depth++; // Enter
  const char *event_name = luaL_checkstring(L, 1);
  int return_count = 0; // How many values we are returning to Lua

  for (int i = 0; i < hook_count; i++) {
    if (strcmp(hooks[i].hook_name, event_name) == 0) {
      lua_State *targetL = hooks[i].plugin->L;

      lua_getglobal(targetL, hooks[i].lua_func_name);
      copy_table_between_states(L, targetL, 2);

      if (lua_pcall(targetL, 1, 1, 0) != LUA_OK) {
        const char *err = lua_tostring(targetL, -1);
        lua_pushnil(L);
        lua_pushstring(L, err);
        return_count = 2; // Return nil + error
        goto exit;        // JUMP TO CLEANUP
      }

      copy_value_back(targetL, L, -1);
      lua_pop(targetL, 1);

      return_count = 1; // Return the result
      goto exit;        // JUMP TO CLEANUP
    }
  }

exit:
  call_stack_depth--; // ALWAYS DECREMENT BEFORE LEAVING
  return return_count;
}