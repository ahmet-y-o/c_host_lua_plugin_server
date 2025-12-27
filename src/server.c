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

  struct MHD_Response *response = NULL;
  int status_code = 200;

  PluginManager *pm = (PluginManager *)closure;

  for (int i = 0; i < pm->count; i++) {
    const char *p_name = pm->list[i]->name;
    size_t name_len = strlen(p_name);
    const char *relative_url = url + 1 + name_len;

    // 1. Check if URL starts with "/plugin_name"
    if (url[0] == '/' && strncmp(url + 1, p_name, name_len) == 0) {
      const char *after_plugin =
          url + 1 + name_len; // Pointing at what's after "/carddav"

      // CHECK FOR STATIC BYPASS: Does it start with "/static/"?
      if (strncmp(after_plugin, "/static/", 8) == 0) {
        const char *filename = after_plugin + 8; // The part after "/static/"

        // 1. Build the absolute path to the file
        char file_path[512];
        snprintf(file_path, sizeof(file_path), "%s/static/%s",
                 pm->list[i]->path, filename);

        // 2. Try to serve it directly from C
        if (serve_static_file(file_path, connection) == MHD_YES) {
          return MHD_YES; // Success! No Lua needed.
        }

        // If file doesn't exist, we can either fall through to 404 or let Lua
        // try
      }

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
        printf("Plugin: %s | Sending to Lua: %s\n", p_name, relative_url);
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
          puts(body_str);
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
  if (response == NULL) {
    // --- 2. FALLBACK TO DEFAULT PLUGIN ---
    for (int i = 0; i < pm->count; i++) {
      if (strcmp(pm->list[i]->name, "default") == 0) {

        // For the default plugin, we pass the URL exactly as is
        // We don't strip anything because there is no prefix.

        // Check for static first: /static/style.css
        if (strncmp(url, "/static/", 8) == 0) {
          char full_path[1024];
          snprintf(full_path, sizeof(full_path), "%s/static/%s",
                   pm->list[i]->path, url + 8);
          if (serve_static_file(full_path, connection) == MHD_YES)
            return MHD_YES;
        }

        // Otherwise, call Lua handle_request
        // Note: Pass the original 'url' here!

        lua_State *L = pm->list[i]->L;
        lua_getglobal(L, "app");
        if (!lua_istable(L, -1)) {
          lua_pop(L, 1);
          continue;
        }

        lua_getfield(L, -1, "handle_request");

        lua_newtable(L);
        lua_pushstring(L, "url");
        lua_pushstring(L, url); // Use the stripped version
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

          break;
        }
        lua_pop(L, 2);
      }
    }
  }
  // --- 3. FINAL QUEUEING ---
  if (response != NULL) {
    // A plugin (specific or default) successfully created a response
    enum MHD_Result ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    return ret;
  }

  // --- 4. ULTIMATE 404 ---
  // If even 'default' isn't there or failed
  struct MHD_Response *res = MHD_create_response_from_buffer(
      13, "Not Found 404", MHD_RESPMEM_MUST_COPY);
  enum MHD_Result ret = MHD_queue_response(connection, 404, res);
  MHD_destroy_response(res);
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