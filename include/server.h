#ifndef SERVER_H
#define SERVER_H
#include <microhttpd.h>
#include "plugin_manager.h"


struct MHD_Daemon* start_server(PluginManager *pm);
enum MHD_Result respond(void *cls, struct MHD_Connection *connection,
                      const char *url, const char *method,
                      const char *version, const char *upload_data,
                      size_t *upload_data_size, void **con_cls);



#endif