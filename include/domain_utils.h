#ifndef DOMAIN_UTILS_H
#define DOMAIN_UTILS_H

#include <time.h>

#include "rental_system.h"

int validate_phone(const char *phone);
int validate_id_card(const char *idCard);
int validate_gender(const char *gender);
void mask_id_card(const char *idCard, char *out, int outSize);

int parse_date(const char *s, struct tm *t);
int parse_datetime(const char *s, struct tm *t);
int validate_date(const char *s);
int validate_datetime(const char *s);

time_t date_to_time(const char *s);
time_t datetime_to_time(const char *s);
int overlaps(time_t s1, time_t e1, time_t s2, time_t e2);
int compare_date_str(const char *a, const char *b);
double overlap_days(time_t s1, time_t e1, time_t s2, time_t e2);
double rental_duration_days(const Rental *r);

#endif
