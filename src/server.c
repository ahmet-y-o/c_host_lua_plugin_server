#include <microhttpd.h>
#include "server.h"
#include "plugin_manager.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <lua.h>      /* The core Lua VM functions */
#include <lualib.h>   /* Access to standard Lua libraries (math, io, etc) */
#include <lauxlib.h>  /* Helper functions for common tasks */

struct MHD_Daemon* start_server(PluginManager *pm) {
    struct MHD_Daemon *daemon;
    daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD,
        8888, // port
        NULL,
        NULL,
        respond, // callback function
        pm, // closure (pass data here)
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
    
    // iterate over plugins
    for (int i = 0; i < pm->count; i++) {
        size_t len = strlen(pm->list[i]->name) + 1;
        char *full_url = malloc(len);
        sprintf(full_url, "/%s", pm->list[i]->name);
        if (strncmp(url, full_url, len) == 0) {
            // Check if it's an exact match OR a sub-path
            if (url[len] == '\0' || url[len] == '/') {
                // This is a valid match for /plugin_name or /plugin_name/anything
                lua_State *L = pm->list[i]->L; 
                // Call the 'handle' function in Lua
                // get the function called handle
                lua_getglobal(L, "handle");
                // TODO: instead of passing a url, pass request object
                // construct a lua table
                // including a url, request-headers, method and if exists, post body
                lua_newtable(L);

                lua_pushstring(L, "url");
                lua_pushstring(L, url);
                lua_settable(L, -3);    // Pass the URL

                lua_pushstring(L, "method");
                lua_pushstring(L, method);
                lua_settable(L, -3);

                if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
                    fprintf(stderr, "Error: %s\n", lua_tostring(L, -1));
                    lua_pop(L, 1); // Remove error message
                }

                // Ensure it's a table
                if (lua_istable(L, -1)) {
                    // --- Get the Body ---
                    // This looks for "body" inside the table at -1 and pushes result to -1
                    lua_getfield(L, -1, "body");
                    if (lua_isstring(L, -1)) {
                        size_t len;
                        const char* lua_str = lua_tolstring(L, -1, &len);

                        // Allocate memory dynamically based on the length Lua gave us
                        char *lua_body_content = malloc(len + 1); 
                        if (lua_body_content) {
                            memcpy(lua_body_content, lua_str, len);
                            lua_body_content[len] = '\0'; // Manually null-terminate
                            
                            response = MHD_create_response_from_buffer(len, lua_body_content, MHD_RESPMEM_MUST_COPY);

                            free(lua_body_content); // Don't forget to free!
                        }
                    } else {
                        // value of body is not a string
                        response = MHD_create_response_from_buffer(21, "body must be a string", MHD_RESPMEM_MUST_COPY);
                    }
                    lua_pop(L, 1); // Clean the stack

                    // --- Get the Status
                    lua_getfield(L, -1, "status");
                    if (lua_isinteger(L, -1)) {
                        lua_Integer lua_int = lua_tointeger(L, -1);
                        status_code = lua_int;
                    }
                    lua_pop(L, 1);

                    // --- Get the Headers
                    lua_getfield(L, -1, "headers");
                    if (lua_istable(L, -1)) {
                        // push nil to start the traversal
                        lua_pushnil(L);

                        // lua_next pops the key and pushes key + value
                        // The table is now at -3, the key at -2, and the value at -1
                        while (lua_next(L, -2) != 0) {
                            
                            // Ensure key and value are strings
                            if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
                                const char *key = lua_tostring(L, -2);
                                const char *val = lua_tostring(L, -1);
                                MHD_add_response_header(response, key, val);
                            }

                            // 4. Pop the VALUE, but keep the KEY for the next iteration
                            lua_pop(L, 1);
                        }
                    }
                    lua_pop(L, 1);
                    
                } else {
                    puts("not a table");
                    // Handle case where Lua didn't return a table (fallback)
                    response = MHD_create_response_from_buffer(11, "Error: No Tbl", MHD_RESPMEM_MUST_COPY);
                    ret = MHD_queue_response(connection, 500, response);
                    break;
                }
                
                // 4. Create the response. 
                // Use MHD_RESPMEM_MUST_COPY because lua_str will be 
                // invalidated when we pop it or when Lua GC runs.
                /* response = MHD_create_response_from_buffer(resp_len, 
                                                        (void*)lua_str, 
                                                        MHD_RESPMEM_MUST_COPY);
                                                        */
                
                lua_pop(L, 1); // Clean Lua stack
                free(full_url);
                break;
            }
        }
        free(full_url);
    }
    if (response == NULL) {
        response = MHD_create_response_from_buffer(5, "HELLO", MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "text/html");
    }

    // 3. Set the Content-Type header to plain text
    // TODO: delete this, make it possible for lua plugin to determine instead
    

    // 4. Send the response with an HTTP 200 OK status
    // TODO: delete this, make it possible for lua plugin to determine instead
    ret = MHD_queue_response(connection, status_code, response);
    
    // 5. Clean up the response object
    MHD_destroy_response(response);

    return ret;
}