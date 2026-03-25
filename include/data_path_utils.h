#ifndef DATA_PATH_UTILS_H
#define DATA_PATH_UTILS_H

#include <stddef.h>

int data_path_file_exists(const char *file);
void data_path_setup_from_argv(char *outPath, size_t outSize, const char *argv0, const char *defaultFileName);

#endif
