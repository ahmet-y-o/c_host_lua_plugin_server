#include "server.h"
#include "plugin_manager.h"
#include <lauxlib.h> /* Helper functions for common tasks */
#include <lua.h>     /* The core Lua VM functions */
#include <lualib.h>  /* Access to standard Lua libraries (math, io, etc) */
#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

  struct MHD_Response *response = NULL;
  enum MHD_Result ret;
  int status_code = 200;

  PluginManager *pm = (PluginManager *)closure;

  for (int i = 0; i < pm->count; i++) {
    const char *p_name = pm->list[i]->name;
    size_t name_len = strlen(p_name);

    // 1. Check if URL starts with "/plugin_name"
    if (url[0] == '/' && strncmp(url + 1, p_name, name_len) == 0) {
      // Check if it's exactly "/name" or starts with "/name/"
      char next_char = url[name_len + 1];
      if (next_char == '\0' || next_char == '/') {

        const char *relative_url = url + 1 + name_len;
        if (relative_url[0] == '\0')
          relative_url = "/";

        lua_State *L = pm->list[i]->L;
        lua_getglobal(L, "app");
        if (!lua_istable(L, -1)) {
          lua_pop(L, 1);
          continue;
        }

        lua_getfield(L, -1, "handle_request");

        lua_newtable(L);
        lua_pushstring(L, "url");
        lua_pushstring(L, relative_url); // Use the stripped version
        lua_settable(L, -3);
        lua_pushstring(L, "method");
        lua_pushstring(L, method);
        lua_settable(L, -3);

        if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
          fprintf(stderr, "Lua Error: %s\n", lua_tostring(L, -1));
          lua_pop(L, 2);
          continue;
        }

        if (lua_istable(L, -1)) {
          // Get Status
          lua_getfield(L, -1, "status");
          status_code = (int)luaL_optinteger(L, -1, 200);
          lua_pop(L, 1);

          // Get Body
          lua_getfield(L, -1, "body");
          size_t body_len;
          const char *body_str = lua_tolstring(L, -1, &body_len);
          response = MHD_create_response_from_buffer(body_len, (void *)body_str,
                                                     MHD_RESPMEM_MUST_COPY);
          lua_pop(L, 1);

          // Get Headers
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
          lua_pop(L, 2); // pop result table and 'app' table

          break; // Match found, exit loop
        }
        lua_pop(L, 2);
      }
    }
  }

  // --- Final Step: Send whatever we found (or fallback) ---
  if (response == NULL) {
    status_code = 404;
    response = MHD_create_response_from_buffer(22, "404 - Plugin Not Found",
                                               MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(response, "Content-Type", "text/plain");
  }

  ret = MHD_queue_response(connection, status_code, response);
  MHD_destroy_response(response);

  return ret;
}