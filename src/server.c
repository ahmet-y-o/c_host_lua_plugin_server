#include "server.h"
#include "plugin_manager.h"
#include <fcntl.h>
#include <lauxlib.h> /* Helper functions for common tasks */
#include <lua.h>     /* The core Lua VM functions */
#include <lualib.h>  /* Access to standard Lua libraries (math, io, etc) */
#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct MHD_Daemon *start_server(PluginManager *pm) {
  struct MHD_Daemon *daemon;
  daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD,
                            8888, // port
                            NULL, NULL,
                            respond, // callback function
                            pm,      // closure (pass data here)
                            MHD_OPTION_END);
  return daemon;
}

// This function is called for every incoming request
enum MHD_Result respond(void *closure, struct MHD_Connection *connection,
                        const char *url, const char *method,
                        const char *version, const char *upload_data,
                        size_t *upload_data_size, void **con_cls) {

  PluginManager *pm = (PluginManager *)closure;
  struct MHD_Response *response = NULL;
  int status_code = 200;

  // 1. TRY SPECIFIC PLUGINS
  for (int i = 0; i < pm->count; i++) {
    Plugin *p = pm->list[i];
    if (strcmp(p->name, "default") == 0)
      continue;

    size_t len = strlen(p->name);
    if (url[0] == '/' && strncmp(url + 1, p->name, len) == 0) {
      const char *after = url + 1 + len;

      // Static check
      if (strncmp(after, "/static/", 8) == 0) {
        char path[512];
        snprintf(path, sizeof(path), "%s/static/%s", p->path, after + 8);
        if (serve_static_file(path, connection) == MHD_YES)
          return MHD_YES;
      }

      // Lua check (Normalize URL to "/" if empty after name)
      const char *rel_url = (*after == '\0') ? "/" : after;
      response = call_plugin_logic(p, rel_url, method, &status_code);
      if (response)
        break;
    }
  }

  // 2. FALLBACK TO DEFAULT
  if (!response) {
    for (int i = 0; i < pm->count; i++) {
      if (strcmp(pm->list[i]->name, "default") == 0) {
        if (strncmp(url, "/static/", 8) == 0) {
          char path[512];
          snprintf(path, sizeof(path), "%s/static/%s", pm->list[i]->path,
                   url + 8);
          if (serve_static_file(path, connection) == MHD_YES)
            return MHD_YES;
        }
        response = call_plugin_logic(pm->list[i], url, method, &status_code);
        break;
      }
    }
  }

  // 3. SEND RESPONSE OR 404
  if (!response) {
    status_code = 404;
    response = MHD_create_response_from_buffer(13, "Not Found 404",
                                               MHD_RESPMEM_MUST_COPY);
  }

  enum MHD_Result ret = MHD_queue_response(connection, status_code, response);
  MHD_destroy_response(response);
  return ret;
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

enum MHD_Result serve_static_file(const char *path,
                                  struct MHD_Connection *connection) {
  int fd = open(path, O_RDONLY);
  if (fd == -1) {
    return MHD_NO; // File not found, C will then try Lua or return 404
  }

  // Get file size
  struct stat st;
  fstat(fd, &st);

  struct MHD_Response *response = MHD_create_response_from_fd(st.st_size, fd);
  MHD_add_response_header(response, "Content-Type", get_mime_type(path));

  enum MHD_Result ret = MHD_queue_response(connection, 200, response);
  MHD_destroy_response(response);
  return ret;
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
  lua_pop(L, 1); // pop headers
  return response;
}

// Helper: Sets up the 'req' table and calls handle_request
struct MHD_Response *call_plugin_logic(Plugin *p, const char *url,
                                       const char *method, int *status_out) {
  lua_State *L = p->L;
  lua_getglobal(L, "app");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
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

  if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
    fprintf(stderr, "Lua Error: %s\n", lua_tostring(L, -1));
    lua_pop(L, 2); // pop error and app table
    return NULL;
  }

  struct MHD_Response *res = build_response_from_lua(L, status_out);
  lua_pop(L, 2); // pop result and app table
  return res;
}