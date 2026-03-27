#ifndef DOMAIN_UTILS_H
#define DOMAIN_UTILS_H

/*
 * 文件: domain_utils.h
 * 定义内容: 领域通用校验、日期时间解析与区间计算接口声明。
 * 后续用途: 业务层统一复用，保证输入校验和时间计算口径一致。
 */

#include <time.h>

#include "rental_system.h"

/* 身份与基础信息校验/脱敏。 */
int validate_phone(const char *phone);
int validate_id_card(const char *idCard);
int validate_gender(const char *gender);
void mask_id_card(const char *idCard, char *out, int outSize);

/* 日期时间解析与格式校验。 */
int parse_date(const char *s, struct tm *t);
int parse_datetime(const char *s, struct tm *t);
int validate_date(const char *s);
int validate_datetime(const char *s);

/* 时间转换与区间工具。 */
time_t date_to_time(const char *s);
time_t datetime_to_time(const char *s);
int overlaps(time_t s1, time_t e1, time_t s2, time_t e2);
int compare_date_str(const char *a, const char *b);
double overlap_days(time_t s1, time_t e1, time_t s2, time_t e2);
double rental_duration_days(const Rental *r);

#endif
