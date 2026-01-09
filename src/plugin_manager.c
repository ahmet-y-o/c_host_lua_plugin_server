#include "plugin_manager.h"
#include "applua_src.h"
#include "etlua_src.h"
#include "lua_helpers.h"
#include <dirent.h>
#include <lua.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

PluginManager *create_manager() {
  // 1. Allocate the manager structure itself
  PluginManager *pm = calloc(1, sizeof(PluginManager));
  if (pm == NULL)
    return NULL;
  // 2. Allocate the internal array of plugins
  pm->plugin_list = calloc(4, sizeof(Plugin *));
  if (pm->plugin_list == NULL) {
    free(pm);
    return NULL;
  }
  // 3. Allocate the internall array of hooks
  pm->hook_list = calloc(4, sizeof(HookRegistration *));
  if (pm->hook_list == NULL) {
    free(pm->plugin_list);
    free(pm);
    return NULL;
  }
  pm->hook_capacity = 4;
  pm->hook_count = 0;
  pm->plugin_count = 0;
  pm->plugin_capacity = 4;
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

void destroy_hook(HookRegistration *h) {
  if (h == NULL) {
    return;
  }
  free(h->hook_name);
  free(h->lua_func_name);
  free(h);
}

void destroy_manager(PluginManager *pm) {
  if (pm == NULL)
    return;

  // 1. Clean up each plugin (Close Lua states)
  for (int i = 0; i < pm->plugin_count; i++) {
    if (pm->plugin_list[i]) {
      destroy_plugin(pm->plugin_list[i]);
    }
  }

  // 2. Free the internal array
  free(pm->plugin_list);

  // 3. Free each hook registarion
  for (int i = 0; i < pm->hook_count; i++) {
    if (pm->hook_list[i]) {
      destroy_hook(pm->hook_list[i]);
    }
  }

  free(pm->hook_list);

  // 5. Free the manager itself
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

  // --- At this point, everything in script.lua is loaded into memory ---
  free(plugin_file_path);
  return p;
}

static bool double_capacity(PluginManager *pm) {
  int new_capacity = pm->plugin_capacity * 2;

  // use realloc the resize the array
  Plugin **new_list = realloc(pm->plugin_list, sizeof(Plugin *) * new_capacity);

  if (new_list != NULL) {
    pm->plugin_list = new_list;
    pm->plugin_capacity = new_capacity;
    return true;
  }
  return false;
}

static bool add_plugin(PluginManager *pm, Plugin *p) {
  if (pm == NULL || p == NULL)
    return false;

  if (pm->plugin_count >= pm->plugin_capacity) {
    if (!double_capacity(pm)) {
      return false;
    }
  }

  // Check if we have room (in case realloc failed)
  // and use correct assignment syntax
  if (pm->plugin_count < pm->plugin_capacity) {
    pm->plugin_list[pm->plugin_count] = p;
    pm->plugin_count++;

    return true;
  }
  return false;
}

void refresh_plugins(PluginManager *pm) {
  if (!pm)
    return;

  // 1. Clear existing hooks (Assuming hook_list is an array of POINTERS)
  for (int i = 0; i < pm->hook_count; i++) {
    if (pm->hook_list[i]) {
      destroy_hook(pm->hook_list[i]);
      pm->hook_list[i] = NULL;
    }
  }
  pm->hook_count = 0;
  // 2. Clean up plugins
  for (int i = 0; i < pm->plugin_count; i++) {
    if (pm->plugin_list[i]) {
      destroy_plugin(pm->plugin_list[i]);
      pm->plugin_list[i] = NULL;
    }
  }
  pm->plugin_count = 0;



  DIR *dp = opendir("./plugins");
  if (!dp) {
    perror("Directory error");
    return;
  }
  struct dirent *ep;
  char path_buffer[1024];
  while ((ep = readdir(dp))) {
    if (ep->d_name[0] == '.')
      continue;

    snprintf(path_buffer, sizeof(path_buffer), "./plugins/%s/plugin.lua",
             ep->d_name);

    if (access(path_buffer, R_OK) == 0) {
      // Create plugin (Ensure create_plugin NO LONGER calls pcall/dofile)
      snprintf(path_buffer, sizeof(path_buffer), "./plugins/%s/", ep->d_name);
      Plugin *p = create_plugin(ep->d_name, path_buffer);
      if (!p)
        continue;
      // Push context to Lua1
      lua_pushlightuserdata(p->L, p);
      lua_pushlightuserdata(p->L, pm);
      lua_pushcclosure(p->L, l_register_hook, 2); // Corrected to 2 upvalues
      lua_setglobal(p->L, "c_register_hook");
      lua_pushlightuserdata(p->L, p);
      lua_pushlightuserdata(p->L, pm);
      lua_pushcclosure(p->L, l_call_hook, 2);
      lua_setglobal(p->L, "c_call_hook");
      // Execute the script now that C functions are bound

      char script_path[1024];
      snprintf(script_path, sizeof(script_path), "%s/plugin.lua", path_buffer);
      if (luaL_dofile(p->L, script_path) != LUA_OK) {
        fprintf(stderr, "Lua Error: %s\n", lua_tostring(p->L, -1));
        destroy_plugin(p);
      } else {
        add_plugin(pm, p);
      }
    }
  }
  closedir(dp);
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

void monitor_plugin_memory(PluginManager *pm) {
  server_log("MONITOR", "--- Memory Usage Report ---");

  for (int i = 0; i < pm->plugin_count; i++) {
    Plugin *p = pm->plugin_list[i];

    // lua_gc with LUA_GCCOUNT returns memory in Kbytes
    int kbytes = lua_gc(p->L, LUA_GCCOUNT, 0);
    // LUA_GCCOUNTB returns the remainder in bytes
    int bytes = lua_gc(p->L, LUA_GCCOUNTB, 0);

    double total_mb = (kbytes / 1024.0) + (bytes / (1024.0 * 1024.0));

    server_log("MONITOR", "Plugin: [%s] | Usage: %.2f MB", p->name, total_mb);
  }
  server_log("MONITOR", "---------------------------");
}

static int call_stack_depth = 0;
#define MAX_CALL_STACK_DEPTH 10

int l_call_hook(lua_State *L) {
  if (call_stack_depth >= MAX_CALL_STACK_DEPTH) {
    return luaL_error(L,
                      "Critical Error: Max event recursion depth (%d) reached!",
                      MAX_CALL_STACK_DEPTH);
  }
  Plugin *p = (Plugin *)lua_touserdata(L, lua_upvalueindex(1));
  PluginManager *pm = (PluginManager *)lua_touserdata(L, lua_upvalueindex(2));

  call_stack_depth++; // Enter
  const char *event_name = luaL_checkstring(L, 1);
  int return_count = 0; // How many values we are returning to Lua

  for (int i = 0; i < pm->hook_count; i++) {
    if (strcmp(pm->hook_list[i]->hook_name, event_name) == 0) {
      lua_State *targetL = pm->hook_list[i]->plugin->L;

      lua_getglobal(targetL, pm->hook_list[i]->lua_func_name);
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