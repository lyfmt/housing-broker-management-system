#ifndef STORAGE_H
#define STORAGE_H

#include "rental_system.h"

int storage_save(const char *filename, const Database *db);
int storage_load(const char *filename, Database *db);

#endif
