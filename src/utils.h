#ifndef _UTILS_H
#define _UTILS_H
#include <stdlib.h>

char *mkdir_app();
char *mkdir_active_clients(char* app_dir);
char *mkdir_topic(char *app_dir, char *topic);
char *mkpipe_topic(char *app_ir, char *topic, int current_client);

#endif
