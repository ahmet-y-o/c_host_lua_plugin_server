#include "plugin_manager.h"
#include "lua_helpers.h"
#include <cJSON.h>
#include <dirent.h>
#include <lauxlib.h>
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
  pm->hook_list = calloc(5, sizeof(HookRegistration *));
  if (pm->hook_list == NULL) {
    free(pm->plugin_list);
    free(pm);
    return NULL;
  }

  pm->queue = job_queue_init();
  pthread_mutex_init(&pm->lock, NULL);

  pm->hook_capacity = 5;
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

  // 1. SHUTDOWN THE WORKERS FIRST
  // We must stop the threads before we start freeing the data they use!
  if (pm->queue) {
    pthread_mutex_lock(&pm->queue->lock);
    pm->queue->shutdown = true;
    pthread_cond_broadcast(&pm->queue->cond); // Wake up all workers
    pthread_mutex_unlock(&pm->queue->lock);

    // Wait for every worker thread to finish its current job and exit
    for (int i = 0; i < pm->num_workers; i++) {
      pthread_join(pm->worker_threads[i], NULL);
    }
    free(pm->worker_threads);
  }

  // 2. CLEAN UP THE REMAINING JOBS
  // If there were jobs still in the queue, free them now
  Job *curr = pm->queue->head;
  while (curr) {
    Job *next = curr->next;
    free(curr->lua_func_name);
    free(curr->payload);
    free(curr);
    curr = next;
  }
  pthread_mutex_destroy(&pm->queue->lock);
  pthread_cond_destroy(&pm->queue->cond);
  free(pm->queue);

  // 3. CLEAN UP PLUGINS
  for (int i = 0; i < pm->plugin_count; i++) {
    destroy_plugin(pm->plugin_list[i]);
  }
  free(pm->plugin_list);

  // 4. CLEAN UP HOOKS
  for (int i = 0; i < pm->hook_count; i++) {
    destroy_hook(pm->hook_list[i]);
  }
  free(pm->hook_list);

  // 5. FINAL CLEANUP
  pthread_mutex_destroy(&pm->lock);
  free(pm);
}

Plugin *create_plugin(char *name, char *path) {
  // 1. allocate memory
  Plugin *p = calloc(1, sizeof(Plugin));
  pthread_mutex_init(&p->lock, NULL);
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
  // 1. Load the file (compiles it to a chunk on the stack)
  size_t plugin_file_path_size =
      strlen(path) + 12; //  /plugin.lua is 11 characters
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
  // TODO: make dynamic
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
      setup_lua_environment(p->L, p, pm);
      // Execute the script now that C functions are bound

      // TODO: make dynamic
      char script_path[1024];
      snprintf(script_path, sizeof(script_path), "%s/plugin.lua", path_buffer);
      if (luaL_dofile(p->L, script_path) != LUA_OK) {
        fprintf(stderr, "Lua Error: %s\n", lua_tostring(p->L, -1));
        destroy_plugin(p);
      } else {
        apply_plugin_schema(p->L, p);
        add_plugin(pm, p);
      }
    }
  }
  closedir(dp);
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

void json_to_lua_table(lua_State *L, cJSON *item) {
  if (item->type == cJSON_Object) {
    lua_newtable(L);
    cJSON *child = item->child;
    while (child) {
      lua_pushstring(L, child->string);
      json_to_lua_table(L, child);
      lua_settable(L, -3);
      child = child->next;
    }
  } else if (item->type == cJSON_Array) {
    lua_newtable(L);
    int i = 1;
    cJSON *child = item->child;
    while (child) {
      lua_pushinteger(L, i++);
      json_to_lua_table(L, child);
      lua_settable(L, -3);
      child = child->next;
    }
  } else if (item->type == cJSON_String) {
    lua_pushstring(L, item->valuestring);
  } else if (item->type == cJSON_Number) {
    lua_pushnumber(L, item->valuedouble);
  } else if (item->type == cJSON_True) {
    lua_pushboolean(L, 1);
  } else if (item->type == cJSON_False) {
    lua_pushboolean(L, 0);
  } else {
    lua_pushnil(L);
  }
}
void enqueue_job(JobQueue *jq, Job *new_job) {
  // 1. Lock the queue so no other thread can modify it
  pthread_mutex_lock(&jq->lock);

  new_job->next = NULL;

  // 2. Add the job to the end of the linked list
  if (jq->tail == NULL) {
    // Queue was empty
    jq->head = new_job;
    jq->tail = new_job;
  } else {
    // Link the old tail to the new job
    jq->tail->next = new_job;
    jq->tail = new_job;
  }

  jq->count++;

  // 3. Signal ONE worker thread that is waiting in pthread_cond_wait
  // This wakes up a worker to process the job
  pthread_cond_signal(&jq->cond);

  // 4. Unlock
  pthread_mutex_unlock(&jq->lock);
}

int l_trigger_async_event(lua_State *L) {
  // Upvalue 2: The PluginManager
  PluginManager *pm = (PluginManager *)lua_touserdata(L, lua_upvalueindex(2));

  const char *event_name = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);

  // 1. Serialize the data ONCE.
  // We do this here so we don't repeat the work for every listener.
  cJSON *json = lua_table_to_json(L, 2);
  char *json_payload = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);

  int listeners_found = 0;

  // 2. Lock the manager to safely scan the hooks
  pthread_mutex_lock(&pm->lock);

  for (int i = 0; i < pm->hook_count; i++) {
    if (strcmp(pm->hook_list[i]->hook_name, event_name) == 0) {
      // 3. Create a NEW job for EVERY plugin listening to this event
      Job *new_job = malloc(sizeof(Job));
      new_job->plugin = pm->hook_list[i]->plugin;
      new_job->lua_func_name = strdup(pm->hook_list[i]->lua_func_name);
      new_job->payload = strdup(json_payload); // Each job gets its own copy
      new_job->next = NULL;

      // 4. Push directly to the queue
      enqueue_job(pm->queue, new_job);
      listeners_found++;
    }
  }

  pthread_mutex_unlock(&pm->lock);

  // Cleanup the local JSON string
  free(json_payload);

  // Return the number of workers notified to Lua (optional but helpful for
  // debugging)
  lua_pushinteger(L, listeners_found);
  return 1;
}

JobQueue *job_queue_init() {
  JobQueue *jq = malloc(sizeof(JobQueue));
  jq->head = NULL;
  jq->tail = NULL;
  jq->count = 0;
  jq->shutdown = false;

  // Initialize the thread primitives
  pthread_mutex_init(&jq->lock, NULL);
  pthread_cond_init(&jq->cond, NULL);

  return jq;
}

int l_enqueue_job(lua_State *L) {
  Plugin *p = (Plugin *)lua_touserdata(L, lua_upvalueindex(1));
  PluginManager *pm = (PluginManager *)lua_touserdata(L, lua_upvalueindex(2));

  const char *func_name = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);

  Job *new_job = malloc(sizeof(Job));
  new_job->plugin = p; // Just copy the pointer address
  new_job->lua_func_name = strdup(func_name);

  // Serialize table to JSON string
  cJSON *json = lua_table_to_json(L, 2);
  new_job->payload = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);

  enqueue_job(pm->queue, new_job);
  return 0;
}

Job *job_queue_pop(JobQueue *jq) {
  pthread_mutex_lock(&jq->lock);

  // 1. Wait while the queue is empty AND we aren't shutting down
  // We use a 'while' loop to protect against "spurious wakeups"
  while (jq->count == 0 && !jq->shutdown) {
    // This atomically releases the lock and pauses the thread.
    // It will only wake up when enqueue_job calls pthread_cond_signal.
    pthread_cond_wait(&jq->cond, &jq->lock);
  }

  // 2. If we woke up because of a shutdown, return NULL
  if (jq->shutdown) {
    pthread_mutex_unlock(&jq->lock);
    return NULL;
  }

  // 3. Extract the job from the head of the list
  Job *job = jq->head;
  jq->head = job->next;

  if (jq->head == NULL) {
    jq->tail = NULL; // Queue is now completely empty
  }

  jq->count--;

  pthread_mutex_unlock(&jq->lock);
  return job;
}

void *worker_thread(void *arg) {
  PluginManager *pm = (PluginManager *)arg;

  while (1) {
    Job *job = job_queue_pop(pm->queue);
    if (!job)
      break; // Shutdown signal

    // 1. Create a fresh, isolated state for this specific job
    lua_State *L = luaL_newstate();

    // 2. Setup the environment (Libs, C-functions, and this job's identity)
    setup_lua_environment(L, job->plugin, pm);

    // 3. Load the specific plugin's code
    // TODO: make dynamic
    char script_path[1024];
    snprintf(script_path, sizeof(script_path), "%s/plugin.lua",
             job->plugin->path);

    if (luaL_dofile(L, script_path) == LUA_OK) {
      // 4. Look up the function and execute
      lua_getglobal(L, job->lua_func_name);

      if (lua_isfunction(L, -1)) {
        cJSON *json = cJSON_Parse(job->payload);
        if (json) {
          json_to_lua_table(L, json);
          cJSON_Delete(json);

          if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            fprintf(stderr, "Async Error (Plugin: %s): %s\n", job->plugin->name,
                    lua_tostring(L, -1));
          }
        }
      } else {
        fprintf(stderr, "Async Error: Function '%s' not found in %s\n",
                job->lua_func_name, job->plugin->name);
      }
    } else {
      fprintf(stderr, "Async Error: Could not load script %s: %s\n",
              script_path, lua_tostring(L, -1));
    }

    // 5. Destroy the state completely - memory is fully reclaimed
    lua_close(L);

    // 6. Cleanup Job
    free(job->lua_func_name);
    free(job->payload);
    free(job);
  }

  return NULL;
}

void start_worker_pool(PluginManager *pm, int num_workers) {
  pm->worker_threads = malloc(sizeof(pthread_t) * num_workers);
  pm->num_workers = num_workers;

  for (int i = 0; i < num_workers; i++) {
    pthread_create(&pm->worker_threads[i], NULL, worker_thread, pm);
  }
}

void job_queue_shutdown(PluginManager *pm) {
  pthread_mutex_lock(&pm->queue->lock);
  pm->queue->shutdown = true;
  // Wake up everyone so they see the shutdown flag
  pthread_cond_broadcast(&pm->queue->cond);
  pthread_mutex_unlock(&pm->queue->lock);

  for (int i = 0; i < pm->num_workers; i++) {
    pthread_join(pm->worker_threads[i], NULL);
  }
}