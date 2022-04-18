#ifndef _UTILS_H
#define _UTILS_H
#include <stdlib.h>

char *mkdir_app();
char *mkpipe_topic(char *basedir, char *topic, int client);
char *get_dirname_topic(char *basedir, char *topic);

char *get_dirname_app();
char *mkdir_topic(char *basedir, char *topic);

#endif
