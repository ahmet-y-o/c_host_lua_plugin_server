#ifndef SERVER_H
#define SERVER_H
#include <microhttpd.h>
#include "plugin_manager.h"


struct MHD_Daemon* start_server(PluginManager *pm);
enum MHD_Result respond(void *cls, struct MHD_Connection *connection,
                      const char *url, const char *method,
                      const char *version, const char *upload_data,
                      size_t *upload_data_size, void **con_cls);
enum MHD_Result serve_static_file(const char *path, struct MHD_Connection *connection);
const char* get_mime_type(const char *path);
struct MHD_Response* build_response_from_lua(lua_State *L, int *status_out);
struct MHD_Response* call_plugin_logic(Plugin *p, const char *url, const char *method, int *status_out);

#endif