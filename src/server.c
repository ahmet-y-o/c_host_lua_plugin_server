// TODO: flamegraphs benchmarks
// TODO: changelog
#include "server.h"
#include "plugin_manager.h"
#include <fcntl.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
  // Data from MHD
  struct MHD_Connection *connection;
  char *url;
  char *method;

  // Upload buffer logic
  char *upload_data;
  size_t upload_size;

  // Result storage
  struct MHD_Response *response;
  int status_code;
  bool processing_done;

  // References
  PluginManager *pm;
} RequestContext;

void *async_worker(void *arg) {
  RequestContext *ctx = (RequestContext *)arg;

  // 1. TRY SPECIFIC PLUGINS
  for (int i = 0; i < ctx->pm->plugin_count; i++) {
    Plugin *p = ctx->pm->plugin_list[i];
    // TODO verify security of strcmp
    if (strcmp(p->name, "default") == 0) continue;

    size_t len = strlen(p->name);
    // Check if URL starts with /plugin_name
    if (ctx->url[0] == '/' && strncmp(ctx->url + 1, p->name, len) == 0) {
      const char *after = ctx->url + 1 + len;

      // A. Static Check
      if (strncmp(after, "/static/", 8) == 0) {
        char path[512];
        snprintf(path, sizeof(path), "%s/static/%s", p->path, after + 8);
        
        // FIX: Just get the response object, don't queue it yet!
        ctx->response = get_static_response(path); 
        if (ctx->response) {
            ctx->status_code = 200;
            break; // Found it, exit loop
        }
      }

      // B. Lua Check
      const char *rel_url = (*after == '\0') ? "/" : after;
      ctx->response = call_plugin_logic(p, rel_url, ctx->method, &(ctx->status_code),
                                        ctx->upload_data, ctx->upload_size);
      // TODO: should check for ==NULL or like this?
      if (ctx->response) break;
    }
  }

  // 2. FALLBACK TO DEFAULT (If no response yet)
  if (!ctx->response) {
    for (int i = 0; i < ctx->pm->plugin_count; i++) {
      if (strcmp(ctx->pm->plugin_list[i]->name, "default") == 0) {
        
        // Static Fallback
        if (strncmp(ctx->url, "/static/", 8) == 0) {
          char path[512];
          snprintf(path, sizeof(path), "%s/static/%s", ctx->pm->plugin_list[i]->path,
                   ctx->url + 8);
          ctx->response = get_static_response(path);
          if (ctx->response) {
              ctx->status_code = 200;
              break;
          }
        }
        
        // Lua Fallback
        ctx->response = call_plugin_logic(ctx->pm->plugin_list[i], ctx->url,
                                          ctx->method, &(ctx->status_code),
                                          ctx->upload_data, ctx->upload_size);
        break;
      }
    }
  }

  // 3. 404 IF STILL NULL
  if (!ctx->response) {
    ctx->status_code = 404;
    ctx->response = MHD_create_response_from_buffer(13, "Not Found 404",
                                                    MHD_RESPMEM_MUST_COPY);
  }

  // 4. TELL MHD TO RESUME
  ctx->processing_done = true;
  MHD_resume_connection(ctx->connection);

  return NULL; // FIX: Return NULL, not MHD_YES
}

void request_completed(void *cls, struct MHD_Connection *connection,
                       void **con_cls, enum MHD_RequestTerminationCode toe) {
  RequestContext *ctx = (RequestContext *)*con_cls;
  if (ctx) {
    if (ctx->upload_data) free(ctx->upload_data);
    if (ctx->url) free(ctx->url);
    if (ctx->method) free(ctx->method);
    free(ctx);
  }
}

struct MHD_Daemon *start_server(PluginManager *pm) {
  return MHD_start_daemon(
      MHD_USE_INTERNAL_POLLING_THREAD | MHD_ALLOW_SUSPEND_RESUME, 8888, NULL,
      NULL, &respond, pm, MHD_OPTION_NOTIFY_COMPLETED, &request_completed, NULL,
      MHD_OPTION_END);
}

// This function is called for every incoming request
enum MHD_Result respond(void *closure, struct MHD_Connection *connection,
                        const char *url, const char *method,
                        const char *version, const char *upload_data,
                        size_t *upload_data_size, void **con_cls) {
  RequestContext *ctx = *con_cls;

  // 1. Initialize Context on first call
  if (ctx == NULL) {
    ctx = calloc(1, sizeof(RequestContext));
    ctx->pm = (PluginManager *)closure;
    ctx->connection = connection;
    ctx->url = strdup(url);
    ctx->method = strdup(method);
    *con_cls = ctx;
    return MHD_YES;
  }

  // 2. Accumulate Upload Data
  if (*upload_data_size > 0) {
    ctx->upload_data =
        realloc(ctx->upload_data, ctx->upload_size + *upload_data_size + 1);
    memcpy(ctx->upload_data + ctx->upload_size, upload_data, *upload_data_size);
    ctx->upload_size += *upload_data_size;
    ctx->upload_data[ctx->upload_size] = '\0';
    *upload_data_size = 0;
    return MHD_YES;
  }

  // 3. Check if we are returning from suspension
  if (ctx->processing_done) {
    enum MHD_Result ret =
        MHD_queue_response(connection, ctx->status_code, ctx->response);
    MHD_destroy_response(ctx->response);
    return ret;
  }

  // 4. SUSPEND AND DISPATCH
  // In a real app, use a Thread Pool. For now, we'll show a simple pthread.
  pthread_t tid;
  MHD_suspend_connection(connection);
  pthread_create(&tid, NULL, async_worker, ctx);
  pthread_detach(tid);

  return MHD_YES;
}

const char *get_mime_type(const char *path) {
  const char *ext = strrchr(path, '.');
  if (!ext)
    return "application/octet-stream";
  if (strcmp(ext, ".html") == 0)
    return "text/html";
  if (strcmp(ext, ".css") == 0)
    return "text/css";
  if (strcmp(ext, ".js") == 0)
    return "application/javascript";
  if (strcmp(ext, ".png") == 0)
    return "image/png";
  if (strcmp(ext, ".jpg") == 0)
    return "image/jpeg";
  if (strcmp(ext, ".svg") == 0)
    return "image/svg+xml";
  return "application/octet-stream";
}

struct MHD_Response *get_static_response(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd == -1) return NULL;
    
    struct stat sbuf;
    if (fstat(fd, &sbuf) != 0) {
        close(fd);
        return NULL;
    }
    
    // Create response from file descriptor
    struct MHD_Response *response = MHD_create_response_from_fd(sbuf.st_size, fd);
    // Add headers if needed (MHD_add_response_header) here
    return response;
}

// Helper: Extracts data from the Lua 'result' table and builds an MHD response
struct MHD_Response *build_response_from_lua(lua_State *L, int *status_out) {
  if (!lua_istable(L, -1))
    return NULL;

  // 1. Status
  lua_getfield(L, -1, "status");
  *status_out = (int)luaL_optinteger(L, -1, 200);
  lua_pop(L, 1);

  // 2. Body
  lua_getfield(L, -1, "body");
  size_t body_len;
  const char *body_str = lua_tolstring(L, -1, &body_len);
  struct MHD_Response *response = MHD_create_response_from_buffer(
      body_len, (void *)body_str, MHD_RESPMEM_MUST_COPY);
  lua_pop(L, 1);

  // 3. Headers
  lua_getfield(L, -1, "headers");
  if (lua_istable(L, -1)) {
    lua_pushnil(L);
    while (lua_next(L, -2)) {
      MHD_add_response_header(response, lua_tostring(L, -2),
                              lua_tostring(L, -1));
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  return response;
}

// Helper: Sets up the 'req' table and calls handle_request
struct MHD_Response *call_plugin_logic(Plugin *p, const char *url,
                                       const char *method, int *status_out,
                                       char *body_data, size_t body_len) {
  pthread_mutex_lock(&p->lock); // <--- Lock before touching Lua
  lua_State *L = p->L;
  lua_getglobal(L, "app");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    pthread_mutex_unlock(&p->lock); // <--- Unlock after getting the response
    return NULL;
  }

  lua_getfield(L, -1, "handle_request");
  lua_newtable(L);
  lua_pushstring(L, "url");
  lua_pushstring(L, url);
  lua_settable(L, -3);
  lua_pushstring(L, "method");
  lua_pushstring(L, method);
  lua_settable(L, -3);
  lua_pushstring(L, "body");
  if (body_data) {
    // lstring for binary safety
    lua_pushlstring(L, body_data, body_len);
  } else {
    lua_pushstring(L, "");
  }
  lua_settable(L, -3);

  if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
    fprintf(stderr, "Lua Error: %s\n", lua_tostring(L, -1));
    lua_pop(L, 2);
    pthread_mutex_unlock(&p->lock); // <--- Unlock after getting the response
    return NULL;
  }

  struct MHD_Response *res = build_response_from_lua(L, status_out);
  lua_pop(L, 2);
  pthread_mutex_unlock(&p->lock); // <--- Unlock after getting the response
  return res;
}
