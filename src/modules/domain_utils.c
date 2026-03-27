/*
 * 文件: domain_utils.c
 * 定义内容: 领域通用校验与时间计算工具（手机号/身份证/日期时间/区间重叠等）。
 * 后续用途: 为业务层提供统一规则基础，避免重复实现导致口径不一致。
 */
#include "domain_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

int validate_phone(const char *phone) {
    int i;
    int len = (int)strlen(phone);
    if (len != 11) return 0;
    if (phone[0] != '1') return 0;
    for (i = 0; phone[i]; ++i) {
        if (!isdigit((unsigned char)phone[i])) return 0;
    }
    return 1;
}

int validate_id_card(const char *idCard) {
    int i;
    if ((int)strlen(idCard) != 18) return 0;
    for (i = 0; i < 17; ++i) {
        if (!isdigit((unsigned char)idCard[i])) return 0;
    }
    if (!(isdigit((unsigned char)idCard[17]) || idCard[17] == 'X' || idCard[17] == 'x')) return 0;
    return 1;
}

int validate_gender(const char *gender) {
    if (!gender) return 0;
    return strcmp(gender, "男") == 0 || strcmp(gender, "女") == 0;
}

void mask_id_card(const char *idCard, char *out, int outSize) {
    int i;
    if (!idCard || (int)strlen(idCard) < 8) {
        strncpy(out, "***", (size_t)outSize - 1);
        out[outSize - 1] = '\0';
        return;
    }
    for (i = 0; i < outSize; ++i) out[i] = '\0';
    snprintf(out, (size_t)outSize, "%.6s********%.4s", idCard, idCard + 14);
}

static int is_leap(int y) {
    if (y % 400 == 0) return 1;
    if (y % 100 == 0) return 0;
    return y % 4 == 0;
}

static int days_in_month(int y, int m) {
    static const int d[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return (m == 2 && is_leap(y)) ? 29 : d[m - 1];
}

int parse_date(const char *s, struct tm *t) {
    int y, m, d;
    if (sscanf(s, "%d-%d-%d", &y, &m, &d) != 3) return 0;
    if (y < 1970 || m < 1 || m > 12) return 0;
    if (d < 1 || d > days_in_month(y, m)) return 0;
    memset(t, 0, sizeof(*t));
    t->tm_year = y - 1900;
    t->tm_mon = m - 1;
    t->tm_mday = d;
    t->tm_hour = 12;
    return 1;
}

int parse_datetime(const char *s, struct tm *t) {
    int y, m, d, hh, mm;
    if (sscanf(s, "%d-%d-%d %d:%d", &y, &m, &d, &hh, &mm) != 5) return 0;
    if (y < 1970 || m < 1 || m > 12) return 0;
    if (d < 1 || d > days_in_month(y, m)) return 0;
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return 0;
    memset(t, 0, sizeof(*t));
    t->tm_year = y - 1900;
    t->tm_mon = m - 1;
    t->tm_mday = d;
    t->tm_hour = hh;
    t->tm_min = mm;
    return 1;
}

int validate_date(const char *s) {
    struct tm t;
    return parse_date(s, &t);
}

int validate_datetime(const char *s) {
    struct tm t;
    return parse_datetime(s, &t);
}

time_t date_to_time(const char *s) {
    struct tm t;
    if (!parse_date(s, &t)) return (time_t)-1;
    return mktime(&t);
}

time_t datetime_to_time(const char *s) {
    struct tm t;
    if (!parse_datetime(s, &t)) return (time_t)-1;
    return mktime(&t);
}

int overlaps(time_t s1, time_t e1, time_t s2, time_t e2) {
    return !(e1 <= s2 || e2 <= s1);
}

int compare_date_str(const char *a, const char *b) {
    time_t ta = date_to_time(a);
    time_t tb = date_to_time(b);
    if (ta < tb) return -1;
    if (ta > tb) return 1;
    return 0;
}

double overlap_days(time_t s1, time_t e1, time_t s2, time_t e2) {
    time_t s = (s1 > s2) ? s1 : s2;
    time_t e = (e1 < e2) ? e1 : e2;
    if (e <= s) return 0.0;
    return difftime(e, s) / 86400.0;
}

double rental_duration_days(const Rental *r) {
    time_t s, e;
    if (!r) return 0.0;
    s = date_to_time(r->startDate);
    e = date_to_time(r->endDate);
    if (s == (time_t)-1 || e == (time_t)-1 || e <= s) return 0.0;
    return difftime(e, s) / 86400.0;
}
