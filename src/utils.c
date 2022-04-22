#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "sha-256.h"
#include "utils.h"

/******************************************************************************/
/* DECLARATION OF HELPER FUNCTIONS */

/* functions */
char *get_rand_str(size_t size);
char *sha256sum(char *str);

/* scoped variables */
static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static const int charset_length = 36;
static bool seed_not_set = true;

/******************************************************************************/
/* IMPLEMENTATION OF THE LIBRARY */

/**
 * Generate random dirname for the application
 **/
char *get_dirname_app() {
  const char dir_prefix[] = "/tmp/mosquitto";
  char *dir, *dirname;
  size_t dir_length = sizeof(char) * 255;

  /* get random directory name suffix */
  dirname = get_rand_str(16);

  /* set the directory full path by concatenation */
  dir = malloc(dir_length);
  dir[0] = '\0';
  snprintf(dir, dir_length, "%s_%s", dir_prefix, dirname);

  /* free unused memory */
  free(dirname);

  return dir;
}

/**
 * Generate deterministic dirname for the topic
 * (hash the topic and append to the base directory)
 **/
char *get_dirname_topic(char *basedir, char *topic) {
  char *dir, *hash;
  hash = sha256sum(topic);
  dir = malloc(sizeof(char) * 255);

  /* dir = $basedir/$hash */
  sprintf(dir, "%s/%s", basedir, hash);

  /* free unused memory */
  free(hash);

  /* return dirname */
  return dir;
}

char *mkdir_app() {
  char *dir = get_dirname_app();
  mkdir(dir, 0770);
  return dir;
}

char *mkdir_topic(char *basedir, char *topic) {
  char *dir = get_dirname_topic(basedir, topic);
  unsigned int mode = strtol("0770", 0, 8);
  mkdir(dir, mode);
  return dir;
}

char *mkpipe_topic(char *basedir, char *topic, int client) {
  char *dirname = get_dirname_topic(basedir, topic);

  DIR *dir = opendir(dirname);
  if (dir == NULL) {
    free(dirname);
    dirname = mkdir_topic(basedir, topic);
    dir = opendir(dirname);
  }
  closedir(dir);

  size_t pipe_length = sizeof(char) * 255;
  char *pipe_name = malloc(pipe_length);
  sprintf(pipe_name, "%s/%d", dirname, client);

  free(dirname);

  return pipe_name;
}

char *mkdir_active_clients(char* app_dir)
{
  char* active_clients_dir = malloc(sizeof(app_dir)+sizeof(char)*20);
  sprintf(active_clients_dir, "%s/active_clients", app_dir);
  mkdir(active_clients_dir, 0770);
  return active_clients_dir;
}

/******************************************************************************/
/* IMPLEMENTATION OF HELPER FUNCTIONS */

/**
 * Generate a random string
 * (used for temporary file creation)
 **/
char *get_rand_str(size_t size) {
  char *str = malloc(sizeof(char) * (size + 1));

  if (seed_not_set) {
    srand(time(0));
    seed_not_set = false;
  }

  for (size_t n = 0; n < size; n++) {
    int key = rand() % charset_length;
    str[n] = charset[key];
  }

  str[size] = '\0';
  return str;
}

/**
 * Compute the SHA256 hash of a string
 * (used for deterministic file creation)
 **/
char *sha256sum(char *str) {
  /* length of the sha256 hash */
  const int length_hash = SIZE_OF_SHA_256_HASH;
  const int length_hash_str = 2 * length_hash;

  /* variable to store the hash */
  u_int8_t hash[length_hash];

  /* hexadecimal string of the hash */
  char *hash_str = malloc(sizeof(char) * (length_hash_str + 1));

  /* calculate the hash */
  calc_sha_256(hash, str, strlen(str));

  /* convert hash number to hash string */
  int j = 0;
  for (int i = 0; i < length_hash; i += 1) {
    /* this converts a integer to a single hex char */
    char c[3];
    sprintf(c, "%02x", (unsigned int)hash[i]);

    /* store the char in the string */
    hash_str[i * 2] = c[0];
    hash_str[i * 2 + 1] = c[1];
    j += 2;
  }

  /* end of string marker */
  hash_str[length_hash_str] = '\0';

  /* return hash string */
  return hash_str;
}
