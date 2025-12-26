#include <stdio.h>
#include <microhttpd.h>
#include "plugin_manager.h"
#include "server.h"



int main() {

    printf("libmicrohttpd version: %s\n", MHD_get_version());
    
    PluginManager *pm = create_manager();
    if (pm == NULL) {
        printf("create_manager returned null");
    }

    refresh_plugins(pm);

    for (int i = 0; i < pm->count ; i++) {
        printf("%s\t%s\n", pm->list[i]->name, pm->list[i]->path);
    }

    struct MHD_Daemon *daemon;
    daemon = start_server(pm);

    if (NULL == daemon) return 1;

    char c;
    while ((c = getchar()) != EOF) {
        if (c == 'r') {
            printf("Refreshing plugins...\n");
            refresh_plugins(pm);
            printf("Plugins refreshed!\n");
        }
    }

    MHD_stop_daemon(daemon);
    destroy_manager(pm);
    pm = NULL;


    return 0;
}
