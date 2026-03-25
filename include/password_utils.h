#ifndef PASSWORD_UTILS_H
#define PASSWORD_UTILS_H

#include <stddef.h>

#include "rental_system.h"

void password_store(char *dest, size_t size, const char *password);
int password_verify(const char *stored, const char *input);
int normalize_database_passwords(Database *db);
void generate_temporary_password(char *out, size_t size);

#endif
