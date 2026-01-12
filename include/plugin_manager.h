#ifndef PLUGIN_MANAGER_H
#define PLUGIN_MANAGER_H
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <pthread.h>
#include <sqlite3.h>

typedef struct {
    char *name;
    char *path;
    lua_State *L;
    sqlite3 *db;
    pthread_mutex_t lock;
} Plugin;


typedef struct Job {
    Plugin *plugin;       // Direct pointer to the owner
    char *lua_func_name;  // Function to call
    char *payload;        // JSON data
    struct Job *next;
} Job;

typedef struct {
    Job *head;
    Job *tail;
    int count;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool shutdown;
} JobQueue;

typedef struct {
    char *hook_name;
    Plugin *plugin;
    char *lua_func_name; // The function name within that plugin's Lua state
    int priority; // Lower numbers = higher priority (runs sooner)
} HookRegistration;

typedef struct {
    // plugin storage
    Plugin **plugin_list;
    int plugin_count;
    int plugin_capacity;

    // hook storage
    HookRegistration **hook_list;
    int hook_count;
    int hook_capacity;

    // The Job Queue
    JobQueue *queue;
    // Background Worker Management
    pthread_t *worker_threads;
    int num_workers;

    pthread_mutex_t lock;
    pthread_cond_t cond;
    
} PluginManager;

PluginManager* create_manager();
void destroy_manager(PluginManager *pm);
Plugin* create_plugin(char *name, char *path);
void refresh_plugins(PluginManager *pm);
void preload_module(lua_State *L, const char *name, const char *source);
int l_log(lua_State *L);
void register_logger(lua_State *L);
void start_worker_pool(PluginManager *pm, int num_workers);
int l_enqueue_job(lua_State *L);
int l_trigger_async_event(lua_State *L);
int l_register_hook(lua_State *L);
int l_call_hook(lua_State *L);
JobQueue *job_queue_init();

int l_get_mem_usage(lua_State *L);

#endif