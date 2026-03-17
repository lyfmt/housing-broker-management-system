#include <ctype.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#endif

#include "platform_compat.h"
#include "rental_system.h"
#include "storage.h"
#include "ui_utils.h"

static Database g_db;

#define LOGIN_ROLE_ADMIN  1
#define LOGIN_ROLE_AGENT  2
#define LOGIN_ROLE_TENANT 3
#define LOGIN_LOCK_SECONDS 900
#define TENANT_ID_MIN 5000
#define TENANT_ID_MAX INT_MAX

typedef struct {
    int role;
    int id;
    int failed;
    time_t lockedUntil;
} LoginGuard;

static LoginGuard g_guards[1024];
static int g_guardCount = 0;
static char g_data_file[512] = DEFAULT_DATA_FILE;

typedef struct {
    jmp_buf env;
    int active;
} BackContext;

static BackContext *g_back_ctx = NULL;

static void sort_houses(void);
static void query_houses_combo(void);
static void query_house_availability_by_time(void);
static void print_house_detailed(const House *h);
static void search_houses_for_tenant(int tenantId);
static void view_my_appointments(int tenantId);
static int upgrade_demo_agent_id_cards(void);
static int reload_database_for_sync(void);
static int viewing_conflict(const char *dt, int dur, int houseId, int agentId, int ignoreId);
static int append_viewing(const Viewing *v);
static void autosave_default(void);
static int contains_case_insensitive(const char *s, const char *sub);
static const char *viewing_state_text(int s);
static const char *rental_sign_state_text(int s);
static int collect_related_agents_for_house(int houseId, int *ids, int maxN);
static int has_open_contract_for_house(int houseId, int ignoreId);
static int house_is_open_for_viewing(const House *h);
static int appointment_time_is_in_future(const char *dt);
static void format_viewing_feedback(char *out, size_t size, const char *action, const char *now, const char *detail);

static int is_back_token(const char *s) {
    return s && (strcmp(s, "#") == 0 || strcmp(s, "-1") == 0);
}

static int is_back_hash(const char *s) {
    return s && strcmp(s, "#") == 0;
}

static void trigger_back_to_menu(void) {
    if (g_back_ctx && g_back_ctx->active) {
        longjmp(g_back_ctx->env, 1);
    }
}

static int data_file_exists(const char *file) {
    FILE *fp;
    if (!file || !file[0]) return 0;
    fp = fopen(file, "rb");
    if (!fp) return 0;
    fclose(fp);
    return 1;
}

static int str_ends_with(const char *s, const char *suffix) {
    size_t sl, su;
    if (!s || !suffix) return 0;
    sl = strlen(s);
    su = strlen(suffix);
    if (su > sl) return 0;
    return strcmp(s + sl - su, suffix) == 0;
}

static int is_absolute_path(const char *path) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    if ((isalpha((unsigned char)path[0]) && path[1] == ':') ||
        (path[0] == '\\' && path[1] == '\\')) {
        return 1;
    }
    return 0;
#else
    return path[0] == '/';
#endif
}

static void resolve_data_path_to_absolute(void) {
    char cwd[512];
    char absolute[512];

    if (is_absolute_path(g_data_file)) return;
    if (!getcwd(cwd, sizeof(cwd))) return;

    if (snprintf(absolute, sizeof(absolute), "%s%s%s", cwd, PATH_SEPARATOR, g_data_file)
        >= (int)sizeof(absolute)) {
        return;
    }

    strncpy(g_data_file, absolute, sizeof(g_data_file) - 1);
    g_data_file[sizeof(g_data_file) - 1] = '\0';
}

static void normalize_simple_path(char *path) {
    char tmp[512];
    size_t i = 0;
    size_t j = 0;

    if (!path || !path[0]) return;

    while (path[i] && j + 1 < sizeof(tmp)) {
        if ((path[i] == '/' && path[i + 1] == '.' && path[i + 2] == '/') ||
            (path[i] == '\\' && path[i + 1] == '.' && path[i + 2] == '\\')) {
            tmp[j++] = path[i];
            i += 3;
            continue;
        }
        if ((path[i] == '/' && path[i + 1] == '/') || (path[i] == '\\' && path[i + 1] == '\\')) {
            tmp[j++] = path[i];
            while (path[i + 1] == path[i]) i++;
            i++;
            continue;
        }
        tmp[j++] = path[i++];
    }

    tmp[j] = '\0';
    strncpy(path, tmp, 511);
    path[511] = '\0';
}

static void remap_build_dir_data_path(void) {
    const char *suffixes[] = {
        "/build/rental_data.dat",
        "\\build\\rental_data.dat",
        "/build/Debug/rental_data.dat",
        "/build/Release/rental_data.dat",
        "/build/RelWithDebInfo/rental_data.dat",
        "/build/MinSizeRel/rental_data.dat",
        "\\build\\Debug\\rental_data.dat",
        "\\build\\Release\\rental_data.dat",
        "\\build\\RelWithDebInfo\\rental_data.dat",
        "\\build\\MinSizeRel\\rental_data.dat"
    };
    int i;
    normalize_simple_path(g_data_file);
    for (i = 0; i < (int)(sizeof(suffixes) / sizeof(suffixes[0])); ++i) {
        const char *suf = suffixes[i];
        size_t gl, su;
        if (!str_ends_with(g_data_file, suf)) continue;
        gl = strlen(g_data_file);
        su = strlen(suf);
        g_data_file[gl - su] = '\0';
        strncat(g_data_file, "/" DEFAULT_DATA_FILE, sizeof(g_data_file) - strlen(g_data_file) - 1);
        return;
    }
}

static void trim_newline(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static int utf8_char_len(unsigned char c) {
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static int utf8_char_disp_width(const unsigned char *s, int len) {
    if (len <= 0) return 0;
    if (s[0] < 0x80) return 1;
    return 2;
}

static void print_table_cell_utf8(const char *s, int width) {
    const unsigned char *p = (const unsigned char *)s;
    int used = 0;
    while (*p) {
        int clen = utf8_char_len(*p);
        int w;
        int i;
        for (i = 1; i < clen; ++i) {
            if (!p[i]) {
                clen = i;
                break;
            }
        }
        w = utf8_char_disp_width(p, clen);
        if (used + w > width) break;
        fwrite(p, (size_t)clen, 1, stdout);
        p += clen;
        used += w;
    }
    while (used < width) {
        putchar(' ');
        used++;
    }
}

static void read_line(char *buf, int size) {
    if (fgets(buf, size, stdin) == NULL) {
        buf[0] = '\0';
        return;
    }
    trim_newline(buf);
    if (is_back_hash(buf)) {
        trigger_back_to_menu();
    }
}

static void input_non_empty(const char *prompt, char *out, int size) {
    while (1) {
        printf("%s", prompt);
        read_line(out, size);
        if (is_back_token(out)) trigger_back_to_menu();
        if (out[0]) return;
        printf("不能为空，请重新输入。\n");
    }
}

static int input_int(const char *prompt, int minVal, int maxVal) {
    char buf[64], *end;
    long v;
    while (1) {
        printf("%s", prompt);
        read_line(buf, sizeof(buf));
        if (is_back_hash(buf)) trigger_back_to_menu();
        if (strcmp(buf, "-1") == 0 && !(minVal <= -1 && maxVal >= -1)) {
            trigger_back_to_menu();
        }
        v = strtol(buf, &end, 10);
        if (end == buf || *end != '\0') {
            printf("输入无效，请输入整数。\n");
            continue;
        }
        if (v < minVal || v > maxVal) {
            printf("输入越界，范围[%d, %d]。\n", minVal, maxVal);
            continue;
        }
        return (int)v;
    }
}

static double input_double(const char *prompt, double minVal, double maxVal) {
    char buf[64], *end;
    double v;
    while (1) {
        printf("%s", prompt);
        read_line(buf, sizeof(buf));
        if (is_back_hash(buf)) trigger_back_to_menu();
        if (strcmp(buf, "-1") == 0 && !(minVal <= -1.0 && maxVal >= -1.0)) {
            trigger_back_to_menu();
        }
        v = strtod(buf, &end);
        if (end == buf || *end != '\0') {
            printf("输入无效，请输入数字。\n");
            continue;
        }
        if (v < minVal || v > maxVal) {
            printf("输入越界，范围[%.2f, %.2f]。\n", minVal, maxVal);
            continue;
        }
        return v;
    }
}

static int input_yes_no(const char *prompt) {
    char buf[16];
    while (1) {
        printf("%s (y/n): ", prompt);
        read_line(buf, sizeof(buf));
        if (is_back_token(buf)) trigger_back_to_menu();
        if (buf[0] == 'y' || buf[0] == 'Y') return 1;
        if (buf[0] == 'n' || buf[0] == 'N') return 0;
        printf("请输入 y 或 n。\n");
    }
}

static int validate_phone(const char *phone) {
    int i;
    int len = (int)strlen(phone);
    if (len != 11) return 0;
    if (phone[0] != '1') return 0;
    for (i = 0; phone[i]; ++i) {
        if (!isdigit((unsigned char)phone[i])) return 0;
    }
    return 1;
}

static int validate_id_card(const char *idCard) {
    int i;
    if ((int)strlen(idCard) != 18) return 0;
    for (i = 0; i < 17; ++i) {
        if (!isdigit((unsigned char)idCard[i])) return 0;
    }
    if (!(isdigit((unsigned char)idCard[17]) || idCard[17] == 'X' || idCard[17] == 'x')) return 0;
    return 1;
}

static void mask_id_card(const char *idCard, char *out, int outSize) {
    int i;
    if (!idCard || (int)strlen(idCard) < 8) {
        strncpy(out, "***", (size_t)outSize - 1);
        out[outSize - 1] = '\0';
        return;
    }
    for (i = 0; i < outSize; ++i) out[i] = '\0';
    snprintf(out, (size_t)outSize, "%.6s********%.4s", idCard, idCard + 14);
}

static void secure_zero(void *ptr, size_t len) {
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) *p++ = 0;
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

static int parse_date(const char *s, struct tm *t) {
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

static int parse_datetime(const char *s, struct tm *t) {
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

static int validate_date(const char *s) {
    struct tm t;
    return parse_date(s, &t);
}

static int validate_datetime(const char *s) {
    struct tm t;
    return parse_datetime(s, &t);
}

static void get_current_datetime(char *buffer, int size) {
    time_t now = time(NULL);
    struct tm *pt = localtime(&now);
    if (!pt) {
        if (size > 0) buffer[0] = '\0';
        return;
    }
    snprintf(buffer, (size_t)size, "%04d-%02d-%02d %02d:%02d",
             pt->tm_year + 1900, pt->tm_mon + 1, pt->tm_mday, pt->tm_hour, pt->tm_min);
}

static time_t date_to_time(const char *s) {
    struct tm t;
    if (!parse_date(s, &t)) return (time_t)-1;
    return mktime(&t);
}

static time_t datetime_to_time(const char *s) {
    struct tm t;
    if (!parse_datetime(s, &t)) return (time_t)-1;
    return mktime(&t);
}

static int appointment_time_is_in_future(const char *dt) {
    time_t target = datetime_to_time(dt);
    time_t now = time(NULL);
    if (target == (time_t)-1) return 0;
    return difftime(target, now) > 0.0;
}

static int overlaps(time_t s1, time_t e1, time_t s2, time_t e2) {
    return !(e1 <= s2 || e2 <= s1);
}

static int compare_date_str(const char *a, const char *b) {
    time_t ta = date_to_time(a);
    time_t tb = date_to_time(b);
    if (ta < tb) return -1;
    if (ta > tb) return 1;
    return 0;
}

static LoginGuard *get_login_guard(int role, int id) {
    int i;
    for (i = 0; i < g_guardCount; ++i) {
        if (g_guards[i].role == role && g_guards[i].id == id) return &g_guards[i];
    }
    if (g_guardCount >= (int)(sizeof(g_guards) / sizeof(g_guards[0]))) return NULL;
    g_guards[g_guardCount].role = role;
    g_guards[g_guardCount].id = id;
    g_guards[g_guardCount].failed = 0;
    g_guards[g_guardCount].lockedUntil = 0;
    g_guardCount++;
    return &g_guards[g_guardCount - 1];
}

static int login_is_locked(int role, int id) {
    LoginGuard *g = get_login_guard(role, id);
    time_t now = time(NULL);
    if (!g) return 0;
    if (g->lockedUntil > now) {
        long remain = (long)(g->lockedUntil - now);
        printf("账号已锁定，剩余约%ld秒。\n", remain);
        return 1;
    }
    if (g->lockedUntil != 0 && g->lockedUntil <= now) {
        g->lockedUntil = 0;
        g->failed = 0;
    }
    return 0;
}

static void setup_data_file_path(const char *argv0) {
    const char *slash1;
    const char *slash2;
    const char *last;
    size_t dirLen;
    if (!argv0 || !argv0[0]) {
        strncpy(g_data_file, DEFAULT_DATA_FILE, sizeof(g_data_file) - 1);
        g_data_file[sizeof(g_data_file) - 1] = '\0';
    } else {
        slash1 = strrchr(argv0, '/');
        slash2 = strrchr(argv0, '\\');
        last = slash1;
        if (!last || (slash2 && slash2 > last)) last = slash2;
        if (!last) {
            strncpy(g_data_file, DEFAULT_DATA_FILE, sizeof(g_data_file) - 1);
            g_data_file[sizeof(g_data_file) - 1] = '\0';
        } else {
            dirLen = (size_t)(last - argv0 + 1);
            if (dirLen + strlen(DEFAULT_DATA_FILE) >= sizeof(g_data_file)) {
                strncpy(g_data_file, DEFAULT_DATA_FILE, sizeof(g_data_file) - 1);
                g_data_file[sizeof(g_data_file) - 1] = '\0';
            } else {
                memcpy(g_data_file, argv0, dirLen);
                g_data_file[dirLen] = '\0';
                strncat(g_data_file, DEFAULT_DATA_FILE, sizeof(g_data_file) - strlen(g_data_file) - 1);
            }
        }
    }

    resolve_data_path_to_absolute();
    remap_build_dir_data_path();
}

static void login_record_fail(int role, int id) {
    LoginGuard *g = get_login_guard(role, id);
    if (!g) return;
    g->failed++;
    if (g->failed >= MAX_LOGIN_ATTEMPTS) {
        g->lockedUntil = time(NULL) + LOGIN_LOCK_SECONDS;
        g->failed = 0;
        printf("失败次数过多，账号锁定15分钟。\n");
    }
}

static void login_record_success(int role, int id) {
    LoginGuard *g = get_login_guard(role, id);
    if (!g) return;
    g->failed = 0;
    g->lockedUntil = 0;
}

static AgentNode *find_agent(int id) {
    AgentNode *cur;
    for (cur = g_db.agents; cur; cur = cur->next) if (cur->data.id == id) return cur;
    return NULL;
}

static AgentNode *find_agent_by_phone(const char *phone) {
    AgentNode *cur;
    for (cur = g_db.agents; cur; cur = cur->next) {
        if (strcmp(cur->data.phone, phone) == 0) return cur;
    }
    return NULL;
}

static TenantNode *find_tenant(int id) {
    TenantNode *cur;
    for (cur = g_db.tenants; cur; cur = cur->next) if (cur->data.id == id) return cur;
    return NULL;
}

static TenantNode *find_tenant_by_phone(const char *phone) {
    TenantNode *cur;
    for (cur = g_db.tenants; cur; cur = cur->next) {
        if (strcmp(cur->data.phone, phone) == 0) return cur;
    }
    return NULL;
}

static HouseNode *find_house(int id) {
    HouseNode *cur;
    for (cur = g_db.houses; cur; cur = cur->next) if (cur->data.id == id) return cur;
    return NULL;
}

static ViewingNode *find_viewing(int id) {
    ViewingNode *cur;
    for (cur = g_db.viewings; cur; cur = cur->next) if (cur->data.id == id) return cur;
    return NULL;
}

static RentalNode *find_rental(int id) {
    RentalNode *cur;
    for (cur = g_db.rentals; cur; cur = cur->next) if (cur->data.id == id) return cur;
    return NULL;
}

static int id_exists_any(int id) {
    return find_agent(id) || find_tenant(id) || find_house(id) || find_viewing(id) || find_rental(id);
}

static int generate_next_tenant_id(void) {
    int id;
    for (id = TENANT_ID_MIN; id <= TENANT_ID_MAX; ++id) {
        if (!find_tenant(id)) return id;
    }
    return 0;
}

static int generate_next_viewing_id(void) {
    int maxId = 0;
    ViewingNode *v;
    for (v = g_db.viewings; v; v = v->next) {
        if (v->data.id > maxId) maxId = v->data.id;
    }
    return maxId + 1;
}

static const char *tenant_house_state_text(const House *h) {
    if (!h) return "未知";
    if (h->status == HOUSE_VACANT && has_open_contract_for_house(h->id, -1)) return "待签约";
    if (h->status == HOUSE_VACANT) return "可预约";
    if (h->status == HOUSE_RENTED) return "已租";
    if (h->status == HOUSE_PENDING) return "待审核";
    if (h->status == HOUSE_OFFLINE) return "已下架";
    return "未知";
}

static void display_agents_for_selection(void) {
    AgentNode *a;
    ui_section("可选中介列表");
    for (a = g_db.agents; a; a = a->next) {
        printf("ID:%d 姓名:%s 电话:%s\n", a->data.id, a->data.name, a->data.phone);
    }
}

static void input_optional_filter_with_choices(const CategoryList *list, const char *title, char *out, int size) {
    char buf[64];
    int i;
    out[0] = '\0';
    if (list && list->count > 0) {
        printf("%s可选: ", title);
        for (i = 0; i < list->count; ++i) {
            printf("%d.%s", i + 1, list->items[i]);
            if (i != list->count - 1) printf("  ");
        }
        printf("\n");
    }
    printf("%s(输入编号/关键字，可空): ", title);
    read_line(buf, sizeof(buf));
    if (!buf[0] || strcmp(buf, "0") == 0) return;
    {
        char *end = NULL;
        long idx = strtol(buf, &end, 10);
        if (end != buf && *end == '\0' && list && idx >= 1 && idx <= list->count) {
            strncpy(out, list->items[idx - 1], size - 1);
            out[size - 1] = '\0';
            return;
        }
    }
    strncpy(out, buf, size - 1);
    out[size - 1] = '\0';
}

static void collect_city_choices(CategoryList *out) {
    HouseNode *h;
    int i;

    if (!out) return;
    out->count = 0;
    for (h = g_db.houses; h; h = h->next) {
        const char *city = h->data.city;
        if (!city[0]) continue;
        for (i = 0; i < out->count; ++i) {
            if (strcmp(out->items[i], city) == 0) break;
        }
        if (i < out->count) continue;
        if (out->count >= MAX_CATEGORY_ITEMS) break;
        strncpy(out->items[out->count], city, MAX_STR - 1);
        out->items[out->count][MAX_STR - 1] = '\0';
        out->count++;
    }
}

static int company_assign_agent_for_house(const House *house) {
    int related[256];
    int n;
    if (house && house->createdByAgentId > 0 && find_agent(house->createdByAgentId)) {
        return house->createdByAgentId;
    }
    if (house) {
        n = collect_related_agents_for_house(house->id, related, 256);
        if (n > 0) return related[0];
    }
    if (g_db.agents) return g_db.agents->data.id;
    return 0;
}

static void make_appointment_for_tenant(int tenantId, int houseId) {
    HouseNode *h;
    AgentNode *assigned;
    Viewing v;

    if (tenantId <= 0) return;
    if (!reload_database_for_sync()) return;

    h = find_house(houseId);
    if (!h) {
        printf("房源不存在。\n");
        return;
    }
    if (!house_is_open_for_viewing(&h->data)) {
        if (h->data.status == HOUSE_VACANT && has_open_contract_for_house(h->data.id, -1)) {
            printf("该房源已有待签或生效中的合同，暂不可预约。\n");
        } else {
            printf("该房源当前不可预约(仅可预约上架且未签约房源)。\n");
        }
        return;
    }

    memset(&v, 0, sizeof(v));
    v.id = generate_next_viewing_id();
    v.houseId = houseId;
    v.tenantId = tenantId;
    v.agentId = (h->data.createdByAgentId > 0) ? h->data.createdByAgentId : 0;
    while (1) {
        input_non_empty("看房时间(YYYY-MM-DD HH:MM): ", v.datetime, sizeof(v.datetime));
        if (!validate_datetime(v.datetime)) {
            printf("时间格式错误。\n");
            continue;
        }
        if (!appointment_time_is_in_future(v.datetime)) {
            printf("预约时间必须晚于当前时间。\n");
            continue;
        }
        break;
    }
    v.durationMinutes = input_int("时长(分钟): ", 10, 600);

    {
        int mode = input_int("预约中介方式 1指定中介 2公司分配: ", 1, 2);
        if (mode == 1) {
            int aid;
            if (!g_db.agents) {
                printf("当前无可用中介，无法指定。\n");
                return;
            }
            display_agents_for_selection();
            aid = input_int("输入中介ID: ", 1000, 4999);
            if (!find_agent(aid)) {
                printf("中介不存在。\n");
                return;
            }
            v.agentId = aid;
        } else {
            v.agentId = company_assign_agent_for_house(&h->data);
            if (v.agentId == 0) {
                printf("公司分配失败：当前无可用中介。\n");
                return;
            }
            assigned = find_agent(v.agentId);
            if (assigned) {
                printf("系统已分配中介: ID:%d 姓名:%s 电话:%s\n",
                       assigned->data.id, assigned->data.name, assigned->data.phone);
            }
        }
    }

    if (viewing_conflict(v.datetime, v.durationMinutes, v.houseId, v.agentId, -1)) {
        printf("预约冲突。\n");
        return;
    }

    v.status = VIEWING_UNCONFIRMED;
    strcpy(v.tenantFeedback, "-");
    strcpy(v.agentFeedback, "-");
    if (!append_viewing(&v)) {
        printf("内存不足。\n");
        return;
    }
    autosave_default();
    printf("预约成功(预约ID:%d)，等待中介处理。\n", v.id);
}

static void search_houses_for_tenant(int tenantId) {
    char regionKw[MAX_STR], areaKw[MAX_BIG_STR], typeKw[MAX_STR], decoKw[MAX_STR], floorKw[MAX_STR];
    double minP, maxP, minA, maxA;
    int stFilter;
    HouseNode *h;
    int cnt = 0;

    if (tenantId <= 0) return;
    if (!reload_database_for_sync()) return;

    ui_section("查询房源(多条件筛选)");
    input_optional_filter_with_choices(&g_db.regions, "区域", regionKw, sizeof(regionKw));
    printf("地段/小区关键字(可空，示例: 华润/万科/中路): ");
    read_line(areaKw, sizeof(areaKw));
    input_optional_filter_with_choices(&g_db.houseTypes, "户型", typeKw, sizeof(typeKw));
    input_optional_filter_with_choices(&g_db.decorations, "装修", decoKw, sizeof(decoKw));
    if (g_db.floorNotes.count > 0) {
        int i;
        printf("楼层说明可选: ");
        for (i = 0; i < g_db.floorNotes.count; ++i) {
            printf("%d.%s", i + 1, g_db.floorNotes.items[i]);
            if (i != g_db.floorNotes.count - 1) printf("  ");
        }
        printf("\n");
    }
    printf("楼层条件(可输入具体楼层数字，或楼层说明关键字，可空): ");
    read_line(floorKw, sizeof(floorKw));
    minP = input_double("最低价格: ", 0.0, 10000000.0);
    maxP = input_double("最高价格: ", minP, 10000000.0);
    minA = input_double("最小面积: ", 0.0, 100000.0);
    maxA = input_double("最大面积: ", minA, 100000.0);
    printf("状态筛选可选: 0.全部  1.可预约  2.暂不可预约\n");
    stFilter = input_int("请选择状态筛选编号: ", 0, 2);

    printf("\n%-8s %-16s %-14s %-8s %-10s %-8s\n", "ID", "小区", "户型", "面积", "价格", "状态");
    printf("------------------------------------------------------------------------\n");
    for (h = g_db.houses; h; h = h->next) {
        int availableForViewing;
        int stateOk;
        int floorOk = 1;
        if (regionKw[0] && !contains_case_insensitive(h->data.region, regionKw)) continue;
        if (areaKw[0] && !contains_case_insensitive(h->data.community, areaKw) && !contains_case_insensitive(h->data.address, areaKw)) continue;
        if (typeKw[0] && !contains_case_insensitive(h->data.houseType, typeKw)) continue;
        if (decoKw[0] && !contains_case_insensitive(h->data.decoration, decoKw)) continue;
        if (h->data.price < minP || h->data.price > maxP) continue;
        if (h->data.area < minA || h->data.area > maxA) continue;

        availableForViewing = house_is_open_for_viewing(&h->data);
        stateOk = (stFilter == 0) || (stFilter == 1 && availableForViewing) ||
                  (stFilter == 2 && !availableForViewing);
        if (!stateOk) continue;

        if (floorKw[0]) {
            char *end = NULL;
            long fv = strtol(floorKw, &end, 10);
            if (end != floorKw && *end == '\0') {
                floorOk = (h->data.floor == (int)fv);
            } else {
                floorOk = contains_case_insensitive(h->data.floorNote, floorKw);
            }
        }
        if (!floorOk) continue;

        printf("%-8d %-16s %-14s %-8.2f %-10.2f %-8s\n",
               h->data.id, h->data.community, h->data.houseType, h->data.area, h->data.price,
               tenant_house_state_text(&h->data));
        cnt++;
    }
    if (!cnt) {
        printf("未找到匹配房源。\n");
        return;
    }

    {
        int id = input_int("输入房源ID查看详情(0返回): ", 0, 99999999);
        HouseNode *target;
        if (id == 0) return;
        target = find_house(id);
        if (!target) {
            printf("房源不存在。\n");
            return;
        }
        print_house_detailed(&target->data);
        if (input_yes_no("是否预约看房")) {
            make_appointment_for_tenant(tenantId, id);
        }
    }
}

static void view_my_appointments(int tenantId) {
    ViewingNode *v;
    int filter;
    int cnt = 0;
    if (tenantId <= 0) return;
    if (!reload_database_for_sync()) return;

    filter = input_int("状态筛选 0全部 1待处理 2已同意 3已拒绝: ", 0, 3);
    ui_section("我的预约记录");
    for (v = g_db.viewings; v; v = v->next) {
        HouseNode *h;
        int matchStatus;
        if (v->data.tenantId != tenantId) continue;
        matchStatus = (filter == 0) ||
                      (filter == 1 && v->data.status == VIEWING_UNCONFIRMED) ||
                      (filter == 2 && v->data.status == VIEWING_CONFIRMED) ||
                      (filter == 3 && v->data.status == VIEWING_CANCELLED);
        if (!matchStatus) continue;

        h = find_house(v->data.houseId);
        printf("预约ID:%d 时间:%s 状态:%s\n", v->data.id, v->data.datetime, viewing_state_text(v->data.status));
        if (h) {
            printf("  房源:%s | 户型:%s | 价格:%.2f\n", h->data.community, h->data.houseType, h->data.price);
        } else {
            printf("  房源ID:%d(详情已删除/不存在)\n", v->data.houseId);
        }
        printf("  中介ID:%d | 中介回复:%s\n", v->data.agentId, v->data.agentFeedback);
        cnt++;
    }
    if (!cnt) printf("暂无符合条件的预约记录。\n");
}

static int contains_agent_id(const int *ids, int n, int id) {
    int i;
    for (i = 0; i < n; ++i) if (ids[i] == id) return 1;
    return 0;
}

static int collect_related_agents_for_house(int houseId, int *ids, int maxN) {
    int n = 0;
    ViewingNode *v;
    RentalNode *r;
    for (v = g_db.viewings; v; v = v->next) {
        if (v->data.houseId != houseId || v->data.agentId == 0) continue;
        if (!find_agent(v->data.agentId)) continue;
        if (contains_agent_id(ids, n, v->data.agentId)) continue;
        if (n < maxN) ids[n++] = v->data.agentId;
    }
    for (r = g_db.rentals; r; r = r->next) {
        if (r->data.houseId != houseId || r->data.agentId == 0) continue;
        if (!find_agent(r->data.agentId)) continue;
        if (contains_agent_id(ids, n, r->data.agentId)) continue;
        if (n < maxN) ids[n++] = r->data.agentId;
    }
    return n;
}

static void print_agent_brief(const Agent *a) {
    printf("  中介ID:%d 姓名:%s 电话:%s\n", a->id, a->name, a->phone);
}

static void print_house_agents_for_tenant(int houseId) {
    int ids[256];
    int n, i;
    AgentNode *a;
    n = collect_related_agents_for_house(houseId, ids, 256);
    if (n > 0) {
        printf("对应中介(历史/当前服务该房源):\n");
        for (i = 0; i < n; ++i) {
            a = find_agent(ids[i]);
            if (a) print_agent_brief(&a->data);
        }
    } else {
        printf("对应中介暂未形成，当前公司中介如下:\n");
        for (a = g_db.agents; a; a = a->next) print_agent_brief(&a->data);
    }
}

static void tenant_view_houses_with_agents(void) {
    HouseNode *h;
    int cnt = 0;
    ui_section("可预约房源与对应中介");
    for (h = g_db.houses; h; h = h->next) {
        if (!house_is_open_for_viewing(&h->data)) continue;
        print_house_detailed(&h->data);
        print_house_agents_for_tenant(h->data.id);
        printf("\n");
        cnt++;
    }
    if (!cnt) printf("暂无可预约房源。\n");
}

static int append_agent(const Agent *a) {
    AgentNode *n = (AgentNode *)malloc(sizeof(AgentNode));
    AgentNode *cur;
    if (!n) return 0;
    n->data = *a;
    n->next = NULL;
    if (!g_db.agents) g_db.agents = n;
    else {
        for (cur = g_db.agents; cur->next; cur = cur->next) {}
        cur->next = n;
    }
    g_db.agentCount++;
    return 1;
}

static int append_tenant(const Tenant *t) {
    TenantNode *n = (TenantNode *)malloc(sizeof(TenantNode));
    TenantNode *cur;
    if (!n) return 0;
    n->data = *t;
    n->next = NULL;
    if (!g_db.tenants) g_db.tenants = n;
    else {
        for (cur = g_db.tenants; cur->next; cur = cur->next) {}
        cur->next = n;
    }
    g_db.tenantCount++;
    return 1;
}

static int append_house(const House *h) {
    HouseNode *n = (HouseNode *)malloc(sizeof(HouseNode));
    HouseNode *cur;
    if (!n) return 0;
    n->data = *h;
    n->next = NULL;
    if (!g_db.houses) g_db.houses = n;
    else {
        for (cur = g_db.houses; cur->next; cur = cur->next) {}
        cur->next = n;
    }
    g_db.houseCount++;
    return 1;
}

static int append_viewing(const Viewing *v) {
    ViewingNode *n = (ViewingNode *)malloc(sizeof(ViewingNode));
    ViewingNode *cur;
    if (!n) return 0;
    n->data = *v;
    n->next = NULL;
    if (!g_db.viewings) g_db.viewings = n;
    else {
        for (cur = g_db.viewings; cur->next; cur = cur->next) {}
        cur->next = n;
    }
    g_db.viewingCount++;
    return 1;
}

static int append_rental(const Rental *r) {
    RentalNode *n = (RentalNode *)malloc(sizeof(RentalNode));
    RentalNode *cur;
    if (!n) return 0;
    n->data = *r;
    n->next = NULL;
    if (!g_db.rentals) g_db.rentals = n;
    else {
        for (cur = g_db.rentals; cur->next; cur = cur->next) {}
        cur->next = n;
    }
    g_db.rentalCount++;
    return 1;
}

static int remove_viewing(int id) {
    ViewingNode *cur = g_db.viewings;
    ViewingNode *pre = NULL;
    while (cur) {
        if (cur->data.id == id) {
            if (pre) pre->next = cur->next;
            else g_db.viewings = cur->next;
            secure_zero(cur, sizeof(*cur));
            free(cur);
            g_db.viewingCount--;
            return 1;
        }
        pre = cur;
        cur = cur->next;
    }
    return 0;
}

static int remove_rental(int id) {
    RentalNode *cur = g_db.rentals;
    RentalNode *pre = NULL;
    while (cur) {
        if (cur->data.id == id) {
            if (pre) pre->next = cur->next;
            else g_db.rentals = cur->next;
            secure_zero(cur, sizeof(*cur));
            free(cur);
            g_db.rentalCount--;
            return 1;
        }
        pre = cur;
        cur = cur->next;
    }
    return 0;
}

static int remove_agent_node(int id) {
    AgentNode *cur = g_db.agents;
    AgentNode *pre = NULL;
    while (cur) {
        if (cur->data.id == id) {
            if (pre) pre->next = cur->next;
            else g_db.agents = cur->next;
            secure_zero(cur, sizeof(*cur));
            free(cur);
            g_db.agentCount--;
            return 1;
        }
        pre = cur;
        cur = cur->next;
    }
    return 0;
}

static int remove_tenant_node(int id) {
    TenantNode *cur = g_db.tenants;
    TenantNode *pre = NULL;
    while (cur) {
        if (cur->data.id == id) {
            if (pre) pre->next = cur->next;
            else g_db.tenants = cur->next;
            secure_zero(cur, sizeof(*cur));
            free(cur);
            g_db.tenantCount--;
            return 1;
        }
        pre = cur;
        cur = cur->next;
    }
    return 0;
}

static int remove_house_node(int id) {
    HouseNode *cur = g_db.houses;
    HouseNode *pre = NULL;
    while (cur) {
        if (cur->data.id == id) {
            if (pre) pre->next = cur->next;
            else g_db.houses = cur->next;
            secure_zero(cur, sizeof(*cur));
            free(cur);
            g_db.houseCount--;
            return 1;
        }
        pre = cur;
        cur = cur->next;
    }
    return 0;
}

static void clear_all_lists(void) {
    AgentNode *a;
    TenantNode *t;
    HouseNode *h;
    ViewingNode *v;
    RentalNode *r;

    for (a = g_db.agents; a;) {
        AgentNode *nx = a->next;
        secure_zero(a, sizeof(*a));
        free(a);
        a = nx;
    }
    for (t = g_db.tenants; t;) {
        TenantNode *nx = t->next;
        secure_zero(t, sizeof(*t));
        free(t);
        t = nx;
    }
    for (h = g_db.houses; h;) {
        HouseNode *nx = h->next;
        secure_zero(h, sizeof(*h));
        free(h);
        h = nx;
    }
    for (v = g_db.viewings; v;) {
        ViewingNode *nx = v->next;
        secure_zero(v, sizeof(*v));
        free(v);
        v = nx;
    }
    for (r = g_db.rentals; r;) {
        RentalNode *nx = r->next;
        secure_zero(r, sizeof(*r));
        free(r);
        r = nx;
    }

    g_db.agents = NULL;
    g_db.tenants = NULL;
    g_db.houses = NULL;
    g_db.viewings = NULL;
    g_db.rentals = NULL;
    g_db.agentCount = 0;
    g_db.tenantCount = 0;
    g_db.houseCount = 0;
    g_db.viewingCount = 0;
    g_db.rentalCount = 0;
}

static int save_to_file(const char *file) {
    char msg[256];
    ui_loading("正在保存数据");
    if (!storage_save(file, &g_db)) {
        snprintf(msg, sizeof(msg), "写入失败: %s", file);
        ui_error(msg);
        return 0;
    }
    snprintf(msg, sizeof(msg), "保存成功: %s", file);
    ui_success(msg);
    return 1;
}

static int load_from_file(const char *file) {
    char msg[256];
    ui_loading("正在加载数据");
    if (!storage_load(file, &g_db)) {
        snprintf(msg, sizeof(msg), "加载失败: %s", file);
        ui_warn(msg);
        return 0;
    }
    snprintf(msg, sizeof(msg), "加载成功: %s", file);
    ui_success(msg);
    return 1;
}

static void autosave_default(void) {
    save_to_file(g_data_file);
}

static const char *house_state_text(int s) {
    if (s == HOUSE_VACANT) return "空闲";
    if (s == HOUSE_RENTED) return "已出租";
    if (s == HOUSE_PENDING) return "待审核";
    if (s == HOUSE_OFFLINE) return "审核不通过/下架";
    return "未知";
}

static const char *viewing_state_text(int s) {
    if (s == VIEWING_UNCONFIRMED) return "待确认";
    if (s == VIEWING_CONFIRMED) return "已确认";
    if (s == VIEWING_COMPLETED) return "已完成";
    if (s == VIEWING_CANCELLED) return "已取消";
    if (s == VIEWING_MISSED) return "未赴约";
    return "未知";
}

static const char *rental_state_text(int s) {
    if (s == RENTAL_ACTIVE) return "有效";
    if (s == RENTAL_EXPIRED) return "已到期";
    if (s == RENTAL_EARLY) return "提前退租";
    return "未知";
}

static const char *rental_sign_state_text(int s) {
    if (s == RENTAL_SIGN_PENDING) return "待签订";
    if (s == RENTAL_SIGN_CONFIRMED) return "已签订";
    if (s == RENTAL_SIGN_REJECTED) return "已拒签";
    if (s == RENTAL_SIGN_CANCELLED) return "已撤销";
    return "未知";
}

static int has_open_contract_for_house(int houseId, int ignoreId) {
    RentalNode *r;
    for (r = g_db.rentals; r; r = r->next) {
        if (r->data.id == ignoreId) continue;
        if (r->data.houseId != houseId) continue;
        if (r->data.signStatus == RENTAL_SIGN_PENDING) return 1;
        if (r->data.signStatus == RENTAL_SIGN_CONFIRMED && r->data.status == RENTAL_ACTIVE) return 1;
    }
    return 0;
}

static int house_is_open_for_viewing(const House *h) {
    if (!h) return 0;
    if (h->status != HOUSE_VACANT) return 0;
    return !has_open_contract_for_house(h->id, -1);
}

static void append_feedback_fragment(char *out, size_t size, const char *fragment) {
    size_t used;
    if (!out || size == 0 || !fragment) return;
    used = strlen(out);
    if (used >= size - 1) return;
    strncpy(out + used, fragment, size - used - 1);
    out[size - 1] = '\0';
}

static void format_viewing_feedback(char *out, size_t size, const char *action, const char *now, const char *detail) {
    if (!out || size == 0) return;
    out[0] = '\0';
    append_feedback_fragment(out, size, action ? action : "");
    append_feedback_fragment(out, size, "(");
    append_feedback_fragment(out, size, now ? now : "");
    append_feedback_fragment(out, size, ")");
    if (detail && detail[0]) {
        append_feedback_fragment(out, size, ": ");
        append_feedback_fragment(out, size, detail);
    }
}

static int tenant_has_pending_rental(int tenantId) {
    RentalNode *r;
    for (r = g_db.rentals; r; r = r->next) {
        if (r->data.tenantId == tenantId && r->data.signStatus == RENTAL_SIGN_PENDING) return 1;
    }
    return 0;
}

static void print_house_detailed(const House *h) {
    printf("┌─────────────────────────────────────────┐\n");
    printf("│ ID:%d | %s-%s 【%s】\n", h->id, h->city, h->region, h->community);
    printf("├─────────────────────────────────────────┤\n");
    printf("│ 地址: %s\n", h->address);
    printf("│ 位置: %s  %d层(%s)  %s\n",
           h->building, h->floor, h->floorNote, h->unitNo);
    printf("│ 朝向: %s | 户型: %s | 装修: %s\n",
           h->orientation, h->houseType, h->decoration);
    printf("│ 面积: %.2f㎡ | 租金: ¥%.2f/月 | 状态: %s\n",
           h->area, h->price, house_state_text(h->status));
    if (h->createdByAgentId > 0) printf("│ 提交来源: 中介ID %d\n", h->createdByAgentId);
    else printf("│ 提交来源: 管理员直录\n");
    if (h->status == HOUSE_OFFLINE && h->rejectReason[0]) {
        printf("│ 驳回原因: %s\n", h->rejectReason);
    }
    printf("│ 房主: %s (%s)\n", h->ownerName, h->ownerPhone);
    printf("└─────────────────────────────────────────┘\n\n");
}

static void print_viewing_detailed(const Viewing *v) {
    printf("┌───────────────────────────────────┐\n");
    printf("│ 看房ID:%d | 时间:%s\n", v->id, v->datetime);
    printf("├───────────────────────────────────┤\n");
    printf("│ 房源ID:%d | 租客ID:%d | 中介ID:%d\n", v->houseId, v->tenantId, v->agentId);
    printf("│ 时长:%d分钟 | 状态:%s\n", v->durationMinutes, viewing_state_text(v->status));
    if (v->tenantFeedback[0]) printf("│ 租客反馈: %s\n", v->tenantFeedback);
    if (v->agentFeedback[0]) printf("│ 中介反馈: %s\n", v->agentFeedback);
    printf("└───────────────────────────────────┘\n\n");
}

static void print_rental_detailed(const Rental *r) {
    printf("┌─────────────────────────────────────┐\n");
    printf("│ 租约ID:%d | 合同日期:%s\n", r->id, r->contractDate);
    printf("├─────────────────────────────────────┤\n");
    printf("│ 房源ID:%d | 租客ID:%d | 中介ID:%d\n", r->houseId, r->tenantId, r->agentId);
    printf("│ 起租:%s - 到期:%s\n", r->startDate, r->endDate);
    printf("│ 月租:¥%.2f | 合同:%s | 履约:%s\n",
           r->monthlyRent, rental_sign_state_text(r->signStatus), rental_state_text(r->status));
    printf("└─────────────────────────────────────┘\n\n");
}

static int contains_case_insensitive(const char *s, const char *sub) {
    int i, j;
    if (!sub[0]) return 1;
    for (i = 0; s[i]; ++i) {
        for (j = 0; sub[j] && s[i + j]; ++j) {
            if (tolower((unsigned char)s[i + j]) != tolower((unsigned char)sub[j])) break;
        }
        if (!sub[j]) return 1;
    }
    return 0;
}

static int viewing_conflict(const char *dt, int dur, int houseId, int agentId, int ignoreId) {
    ViewingNode *v;
    time_t ns = datetime_to_time(dt);
    time_t ne = ns + dur * 60;
    if (ns == (time_t)-1) return 1;

    for (v = g_db.viewings; v; v = v->next) {
        time_t s, e;
        if (v->data.id == ignoreId) continue;
        if (v->data.status == VIEWING_CANCELLED || v->data.status == VIEWING_COMPLETED || v->data.status == VIEWING_MISSED) continue;
        s = datetime_to_time(v->data.datetime);
        if (s == (time_t)-1) continue;
        e = s + v->data.durationMinutes * 60;

        if (v->data.houseId == houseId && overlaps(ns, ne, s, e)) return 1;
        if (agentId != 0 && v->data.agentId == agentId && overlaps(ns, ne, s, e)) return 1;
    }
    return 0;
}

static void refresh_house_status(int houseId) {
    HouseNode *h = find_house(houseId);
    RentalNode *r;
    if (!h) return;
    if (h->data.status == HOUSE_OFFLINE || h->data.status == HOUSE_PENDING) return;
    h->data.status = HOUSE_VACANT;
    for (r = g_db.rentals; r; r = r->next) {
        if (r->data.houseId == houseId &&
            r->data.signStatus == RENTAL_SIGN_CONFIRMED &&
            r->data.status == RENTAL_ACTIVE) {
            h->data.status = HOUSE_RENTED;
            return;
        }
    }
}

static void update_expired_rentals(void) {
    RentalNode *r;
    time_t now = time(NULL);
    struct tm *pt = localtime(&now);
    char today[11];
    if (!pt) return;
    if (strftime(today, sizeof(today), "%Y-%m-%d", pt) == 0) return;

    for (r = g_db.rentals; r; r = r->next) {
        if (r->data.signStatus == RENTAL_SIGN_CONFIRMED &&
            r->data.status == RENTAL_ACTIVE &&
            compare_date_str(r->data.endDate, today) < 0) {
            r->data.status = RENTAL_EXPIRED;
            refresh_house_status(r->data.houseId);
        }
    }
}

static int reload_database_for_sync(void) {
    if (!storage_load(g_data_file, &g_db)) {
        printf("从数据文件加载失败，无法同步最新数据。\n");
        return 0;
    }
    update_expired_rentals();
    return 1;
}

static void category_add_default(CategoryList *list, const char *value) {
    int i;
    if (list->count >= MAX_CATEGORY_ITEMS) return;
    for (i = 0; i < list->count; ++i) if (strcmp(list->items[i], value) == 0) return;
    strncpy(list->items[list->count], value, MAX_STR - 1);
    list->items[list->count][MAX_STR - 1] = '\0';
    list->count++;
}

static void category_print(const CategoryList *list, const char *title) {
    int i;
    printf("[%s] 共%d项\n", title, list->count);
    for (i = 0; i < list->count; ++i) printf("  %d. %s\n", i + 1, list->items[i]);
}

static int category_pick(const CategoryList *list, const char *title, char *out) {
    int idx;
    if (list->count == 0) {
        printf("分类[%s]为空，请管理员先配置。\n", title);
        return 0;
    }
    category_print(list, title);
    idx = input_int("请选择编号: ", 1, list->count) - 1;
    strcpy(out, list->items[idx]);
    return 1;
}

static void init_defaults(Database *db) {
    Agent a;
    memset(db, 0, sizeof(*db));
    strcpy(db->adminPassword, "admin123");

    category_add_default(&db->regions, "浑南区");
    category_add_default(&db->regions, "和平区");
    category_add_default(&db->regions, "皇姑区");

    category_add_default(&db->floorNotes, "低层");
    category_add_default(&db->floorNotes, "中层");
    category_add_default(&db->floorNotes, "高层");

    category_add_default(&db->orientations, "南");
    category_add_default(&db->orientations, "南北");
    category_add_default(&db->orientations, "东西");

    category_add_default(&db->houseTypes, "一室一卫");
    category_add_default(&db->houseTypes, "二室一厅一卫");
    category_add_default(&db->houseTypes, "三室一厅两卫");

    category_add_default(&db->decorations, "简装");
    category_add_default(&db->decorations, "精装");

    a.id = 1001;
    strcpy(a.name, "张中介");
    strcpy(a.phone, "13800000001");
    strcpy(a.idCard, "210102198803153216");
    strcpy(a.password, DEFAULT_AGENT_PASSWORD);
    append_agent(&a);
}

static void seed_demo_data(void) {
    Agent a;
    Tenant t;
    House h;
    Viewing v;
    Rental r;

    if (!find_agent(1002)) {
        a.id = 1002;
        strcpy(a.name, "李楠");
        strcpy(a.phone, "13800000002");
        strcpy(a.idCard, "210103198907214536");
        strcpy(a.password, DEFAULT_AGENT_PASSWORD);
        append_agent(&a);
    }
    if (!find_agent(1003)) {
        a.id = 1003;
        strcpy(a.name, "王凯");
        strcpy(a.phone, "13800000003");
        strcpy(a.idCard, "210104199001126258");
        strcpy(a.password, DEFAULT_AGENT_PASSWORD);
        append_agent(&a);
    }
    if (!find_agent(1004)) {
        a.id = 1004;
        strcpy(a.name, "许大飞");
        strcpy(a.phone, "13700000001");
        strcpy(a.idCard, "210105198612083519");
        strcpy(a.password, DEFAULT_AGENT_PASSWORD);
        append_agent(&a);
    }

    if (!find_tenant(5001)) {
        t.id = 5001;
        strcpy(t.name, "赵敏");
        strcpy(t.phone, "13911112222");
        strcpy(t.idCard, "210102199512123628");
        strcpy(t.password, "tenant01");
        append_tenant(&t);
    }
    if (!find_tenant(5002)) {
        t.id = 5002;
        strcpy(t.name, "钱浩");
        strcpy(t.phone, "13911113333");
        strcpy(t.idCard, "210103199804053219");
        strcpy(t.password, "tenant02");
        append_tenant(&t);
    }
    if (!find_tenant(5003)) {
        t.id = 5003;
        strcpy(t.name, "孙悦");
        strcpy(t.phone, "13911114444");
        strcpy(t.idCard, "21010419990316842X");
        strcpy(t.password, "tenant03");
        append_tenant(&t);
    }

    if (!find_house(2001)) {
        h.id = 2001;
        strcpy(h.city, "沈阳");
        strcpy(h.region, "浑南区");
        strcpy(h.community, "华润悦府");
        strcpy(h.address, "浑南中路88号");
        strcpy(h.building, "1栋");
        h.floor = 12;
        strcpy(h.unitNo, "1202室");
        strcpy(h.floorNote, "中层");
        strcpy(h.orientation, "南北");
        strcpy(h.houseType, "二室一厅一卫");
        h.area = 89.5;
        strcpy(h.decoration, "精装");
        h.price = 3200;
        strcpy(h.ownerName, "刘先生");
        strcpy(h.ownerPhone, "13788889999");
        h.createdByAgentId = 1001;
        h.status = HOUSE_VACANT;
        h.rejectReason[0] = '\0';
        append_house(&h);
    }
    if (!find_house(2002)) {
        h.id = 2002;
        strcpy(h.city, "沈阳");
        strcpy(h.region, "和平区");
        strcpy(h.community, "万科春河里");
        strcpy(h.address, "南京南街26号");
        strcpy(h.building, "2栋");
        h.floor = 8;
        strcpy(h.unitNo, "803室");
        strcpy(h.floorNote, "中层");
        strcpy(h.orientation, "南");
        strcpy(h.houseType, "一室一卫");
        h.area = 52.0;
        strcpy(h.decoration, "简装");
        h.price = 2100;
        strcpy(h.ownerName, "周女士");
        strcpy(h.ownerPhone, "13677778888");
        h.createdByAgentId = 1002;
        h.status = HOUSE_VACANT;
        h.rejectReason[0] = '\0';
        append_house(&h);
    }
    if (!find_house(2003)) {
        h.id = 2003;
        strcpy(h.city, "沈阳");
        strcpy(h.region, "皇姑区");
        strcpy(h.community, "保利海上五月花");
        strcpy(h.address, "崇山路66号");
        strcpy(h.building, "6栋");
        h.floor = 15;
        strcpy(h.unitNo, "1501室");
        strcpy(h.floorNote, "高层");
        strcpy(h.orientation, "南北");
        strcpy(h.houseType, "三室一厅两卫");
        h.area = 128.0;
        strcpy(h.decoration, "精装");
        h.price = 4500;
        strcpy(h.ownerName, "吴先生");
        strcpy(h.ownerPhone, "13566667777");
        h.createdByAgentId = 1003;
        h.status = HOUSE_RENTED;
        h.rejectReason[0] = '\0';
        append_house(&h);
    }

    if (!find_viewing(3001)) {
        v.id = 3001;
        strcpy(v.datetime, "2026-03-20 14:30");
        v.houseId = 2001;
        v.tenantId = 5001;
        v.agentId = 1002;
        v.durationMinutes = 45;
        v.status = VIEWING_CONFIRMED;
        strcpy(v.tenantFeedback, "-");
        strcpy(v.agentFeedback, "已电话确认");
        append_viewing(&v);
    }
    if (!find_viewing(3002)) {
        v.id = 3002;
        strcpy(v.datetime, "2026-03-22 10:00");
        v.houseId = 2002;
        v.tenantId = 5002;
        v.agentId = 0;
        v.durationMinutes = 30;
        v.status = VIEWING_UNCONFIRMED;
        strcpy(v.tenantFeedback, "-");
        strcpy(v.agentFeedback, "-");
        append_viewing(&v);
    }

    if (!find_rental(4001)) {
        r.id = 4001;
        r.houseId = 2003;
        r.tenantId = 5003;
        r.agentId = 1001;
        strcpy(r.contractDate, "2026-02-15");
        strcpy(r.startDate, "2026-03-01");
        strcpy(r.endDate, "2027-02-28");
        r.monthlyRent = 4300;
        r.status = RENTAL_ACTIVE;
        r.signStatus = RENTAL_SIGN_CONFIRMED;
        append_rental(&r);
    }
    refresh_house_status(2003);
}

static int upgrade_demo_agent_id_cards(void) {
    struct {
        int id;
        const char *idCard;
    } defaults[] = {
        {1001, "210102198803153216"},
        {1002, "210103198907214536"},
        {1003, "210104199001126258"},
        {1004, "210105198612083519"}
    };
    int changed = 0;
    int i;

    for (i = 0; i < (int)(sizeof(defaults) / sizeof(defaults[0])); ++i) {
        AgentNode *a = find_agent(defaults[i].id);
        if (!a || a->data.idCard[0]) continue;
        strncpy(a->data.idCard, defaults[i].idCard, sizeof(a->data.idCard) - 1);
        a->data.idCard[sizeof(a->data.idCard) - 1] = '\0';
        changed = 1;
    }
    return changed;
}

static void list_agents(void) {
    AgentNode *a;
    int shown = 0;
    char masked[32];
    int width = get_terminal_width();
    ui_section("中介列表");
    
    if (width >= 108) {
        printf("+------+----------+-------------+----------------------+\n");
        printf("| "); print_table_cell_utf8("ID", 4); printf(" | ");
        print_table_cell_utf8("姓名", 8); printf(" | ");
        print_table_cell_utf8("电话", 11); printf(" | ");
        print_table_cell_utf8("身份证(脱敏)", 20); printf(" |\n");
        printf("+------+----------+-------------+----------------------+\n");
        for (a = g_db.agents; a; a = a->next) {
            char idBuf[16];
            mask_id_card(a->data.idCard, masked, sizeof(masked));
            snprintf(idBuf, sizeof(idBuf), "%d", a->data.id);
            printf("| "); print_table_cell_utf8(idBuf, 4); printf(" | ");
            print_table_cell_utf8(a->data.name, 8); printf(" | ");
            print_table_cell_utf8(a->data.phone, 11); printf(" | ");
            print_table_cell_utf8(masked, 20); printf(" |\n");
            shown++;
            if (!ui_page_break_if_needed(shown)) break;
        }
        printf("+------+----------+-------------+----------------------+\n");
    } else if (width >= 80) {
        printf("+------+----------+-------------+\n");
        printf("| "); print_table_cell_utf8("ID", 4); printf(" | ");
        print_table_cell_utf8("姓名", 8); printf(" | ");
        print_table_cell_utf8("电话", 11); printf(" |\n");
        printf("+------+----------+-------------+\n");
        for (a = g_db.agents; a; a = a->next) {
            char idBuf[16];
            snprintf(idBuf, sizeof(idBuf), "%d", a->data.id);
            printf("| "); print_table_cell_utf8(idBuf, 4); printf(" | ");
            print_table_cell_utf8(a->data.name, 8); printf(" | ");
            print_table_cell_utf8(a->data.phone, 11); printf(" |\n");
            shown++;
            if (!ui_page_break_if_needed(shown)) break;
        }
        printf("+------+----------+-------------+\n");
    } else {
        printf("+------+----------+\n");
        printf("| "); print_table_cell_utf8("ID", 4); printf(" | ");
        print_table_cell_utf8("姓名", 8); printf(" |\n");
        printf("+------+----------+\n");
        for (a = g_db.agents; a; a = a->next) {
            char idBuf[16];
            snprintf(idBuf, sizeof(idBuf), "%d", a->data.id);
            printf("| "); print_table_cell_utf8(idBuf, 4); printf(" | ");
            print_table_cell_utf8(a->data.name, 8); printf(" |\n");
            shown++;
            if (!ui_page_break_if_needed(shown)) break;
        }
        printf("+------+----------+\n");
    }
}

static void list_tenants(void) {
    TenantNode *t;
    int shown = 0;
    char masked[32];
    int width = get_terminal_width();
    ui_section("租客列表");
    
    if (width >= 100) {
        printf("+--------+----------+-------------+----------------------+\n");
        printf("| "); print_table_cell_utf8("ID", 6); printf(" | ");
        print_table_cell_utf8("姓名", 8); printf(" | ");
        print_table_cell_utf8("电话", 11); printf(" | ");
        print_table_cell_utf8("身份证(脱敏)", 20); printf(" |\n");
        printf("+--------+----------+-------------+----------------------+\n");
        for (t = g_db.tenants; t; t = t->next) {
            char idBuf[16];
            mask_id_card(t->data.idCard, masked, sizeof(masked));
            snprintf(idBuf, sizeof(idBuf), "%d", t->data.id);
            printf("| "); print_table_cell_utf8(idBuf, 6); printf(" | ");
            print_table_cell_utf8(t->data.name, 8); printf(" | ");
            print_table_cell_utf8(t->data.phone, 11); printf(" | ");
            print_table_cell_utf8(masked, 20); printf(" |\n");
            shown++;
            if (!ui_page_break_if_needed(shown)) break;
        }
        printf("+--------+----------+-------------+----------------------+\n");
    } else if (width >= 80) {
        printf("+--------+----------+-------------+\n");
        printf("| "); print_table_cell_utf8("ID", 6); printf(" | ");
        print_table_cell_utf8("姓名", 8); printf(" | ");
        print_table_cell_utf8("电话", 11); printf(" |\n");
        printf("+--------+----------+-------------+\n");
        for (t = g_db.tenants; t; t = t->next) {
            char idBuf[16];
            snprintf(idBuf, sizeof(idBuf), "%d", t->data.id);
            printf("| "); print_table_cell_utf8(idBuf, 6); printf(" | ");
            print_table_cell_utf8(t->data.name, 8); printf(" | ");
            print_table_cell_utf8(t->data.phone, 11); printf(" |\n");
            shown++;
            if (!ui_page_break_if_needed(shown)) break;
        }
        printf("+--------+----------+-------------+\n");
    } else {
        printf("+--------+----------+\n");
        printf("| "); print_table_cell_utf8("ID", 6); printf(" | ");
        print_table_cell_utf8("姓名", 8); printf(" |\n");
        printf("+--------+----------+\n");
        for (t = g_db.tenants; t; t = t->next) {
            char idBuf[16];
            snprintf(idBuf, sizeof(idBuf), "%d", t->data.id);
            printf("| "); print_table_cell_utf8(idBuf, 6); printf(" | ");
            print_table_cell_utf8(t->data.name, 8); printf(" |\n");
            shown++;
            if (!ui_page_break_if_needed(shown)) break;
        }
        printf("+--------+----------+\n");
    }
}

static void list_houses(void) {
    HouseNode *h;
    int shown = 0;
    ui_section("房源列表");
    for (h = g_db.houses; h; h = h->next) {
        print_house_detailed(&h->data);
        shown++;
        if (!ui_page_break_if_needed(shown)) break;
    }
}

static void list_viewings_all(void) {
    ViewingNode *v;
    int shown = 0;
    int width = get_terminal_width();
    ui_section("看房列表");
    
    if (width >= 90) {
        printf("+------+------------------+------+--------+------+--------+----------+\n");
        printf("| "); print_table_cell_utf8("ID", 4); printf(" | ");
        print_table_cell_utf8("时间", 16); printf(" | ");
        print_table_cell_utf8("房源", 4); printf(" | ");
        print_table_cell_utf8("租客", 6); printf(" | ");
        print_table_cell_utf8("中介", 4); printf(" | ");
        print_table_cell_utf8("时长", 6); printf(" | ");
        print_table_cell_utf8("状态", 8); printf(" |\n");
        printf("+------+------------------+------+--------+------+--------+----------+\n");
        for (v = g_db.viewings; v; v = v->next) {
            char idBuf[16], houseBuf[16], tenantBuf[16], agentBuf[16], durBuf[16];
            snprintf(idBuf, sizeof(idBuf), "%d", v->data.id);
            snprintf(houseBuf, sizeof(houseBuf), "%d", v->data.houseId);
            snprintf(tenantBuf, sizeof(tenantBuf), "%d", v->data.tenantId);
            snprintf(agentBuf, sizeof(agentBuf), "%d", v->data.agentId);
            snprintf(durBuf, sizeof(durBuf), "%d", v->data.durationMinutes);
            printf("| "); print_table_cell_utf8(idBuf, 4); printf(" | ");
            print_table_cell_utf8(v->data.datetime, 16); printf(" | ");
            print_table_cell_utf8(houseBuf, 4); printf(" | ");
            print_table_cell_utf8(tenantBuf, 6); printf(" | ");
            print_table_cell_utf8(agentBuf, 4); printf(" | ");
            print_table_cell_utf8(durBuf, 6); printf(" | ");
            print_table_cell_utf8(viewing_state_text(v->data.status), 8); printf(" |\n");
            shown++;
            if (!ui_page_break_if_needed(shown)) break;
        }
        printf("+------+------------------+------+--------+------+--------+----------+\n");
    } else {
        printf("+------+------------------+--------+\n");
        printf("| "); print_table_cell_utf8("ID", 4); printf(" | ");
        print_table_cell_utf8("时间", 16); printf(" | ");
        print_table_cell_utf8("状态", 6); printf(" |\n");
        printf("+------+------------------+--------+\n");
        for (v = g_db.viewings; v; v = v->next) {
            char idBuf[16];
            snprintf(idBuf, sizeof(idBuf), "%d", v->data.id);
            printf("| "); print_table_cell_utf8(idBuf, 4); printf(" | ");
            print_table_cell_utf8(v->data.datetime, 16); printf(" | ");
            print_table_cell_utf8(viewing_state_text(v->data.status), 6); printf(" |\n");
            shown++;
            if (!ui_page_break_if_needed(shown)) break;
        }
        printf("+------+------------------+--------+\n");
    }
}

static void list_rentals_all(void) {
    RentalNode *r;
    int shown = 0;
    int width = get_terminal_width();
    ui_section("租约列表");
    
    if (width >= 100) {
        printf("+------+--------+--------+------+------------+------------+----------+----------+----------+\n");
        printf("| "); print_table_cell_utf8("ID", 4); printf(" | ");
        print_table_cell_utf8("房源", 6); printf(" | ");
        print_table_cell_utf8("租客", 6); printf(" | ");
        print_table_cell_utf8("中介", 4); printf(" | ");
        print_table_cell_utf8("起租", 10); printf(" | ");
        print_table_cell_utf8("到期", 10); printf(" | ");
        print_table_cell_utf8("月租", 8); printf(" | ");
        print_table_cell_utf8("签约", 8); printf(" | ");
        print_table_cell_utf8("状态", 8); printf(" |\n");
        printf("+------+--------+--------+------+------------+------------+----------+----------+----------+\n");
        for (r = g_db.rentals; r; r = r->next) {
            char idBuf[16], houseBuf[16], tenantBuf[16], agentBuf[16], rentBuf[32];
            snprintf(idBuf, sizeof(idBuf), "%d", r->data.id);
            snprintf(houseBuf, sizeof(houseBuf), "%d", r->data.houseId);
            snprintf(tenantBuf, sizeof(tenantBuf), "%d", r->data.tenantId);
            snprintf(agentBuf, sizeof(agentBuf), "%d", r->data.agentId);
            snprintf(rentBuf, sizeof(rentBuf), "%.2f", r->data.monthlyRent);
            printf("| "); print_table_cell_utf8(idBuf, 4); printf(" | ");
            print_table_cell_utf8(houseBuf, 6); printf(" | ");
            print_table_cell_utf8(tenantBuf, 6); printf(" | ");
            print_table_cell_utf8(agentBuf, 4); printf(" | ");
            print_table_cell_utf8(r->data.startDate, 10); printf(" | ");
            print_table_cell_utf8(r->data.endDate, 10); printf(" | ");
            print_table_cell_utf8(rentBuf, 8); printf(" | ");
            print_table_cell_utf8(rental_sign_state_text(r->data.signStatus), 8); printf(" | ");
            print_table_cell_utf8(rental_state_text(r->data.status), 8); printf(" |\n");
            shown++;
            if (!ui_page_break_if_needed(shown)) break;
        }
        printf("+------+--------+--------+------+------------+------------+----------+----------+----------+\n");
    } else if (width >= 80) {
        printf("+------+--------+---+---+--------+--------+\n");
        printf("| "); print_table_cell_utf8("ID", 4); printf(" | ");
        print_table_cell_utf8("房源", 6); printf(" | ");
        print_table_cell_utf8("客", 1); printf(" | ");
        print_table_cell_utf8("介", 1); printf(" | ");
        print_table_cell_utf8("签约", 6); printf(" | ");
        print_table_cell_utf8("状态", 6); printf(" |\n");
        printf("+------+--------+---+---+--------+--------+\n");
        for (r = g_db.rentals; r; r = r->next) {
            char idBuf[16], houseBuf[16], tenantBuf[16], agentBuf[16];
            snprintf(idBuf, sizeof(idBuf), "%d", r->data.id);
            snprintf(houseBuf, sizeof(houseBuf), "%d", r->data.houseId);
            snprintf(tenantBuf, sizeof(tenantBuf), "%d", r->data.tenantId);
            snprintf(agentBuf, sizeof(agentBuf), "%d", r->data.agentId);
            printf("| "); print_table_cell_utf8(idBuf, 4); printf(" | ");
            print_table_cell_utf8(houseBuf, 6); printf(" | ");
            print_table_cell_utf8(tenantBuf, 1); printf(" | ");
            print_table_cell_utf8(agentBuf, 1); printf(" | ");
            print_table_cell_utf8(rental_sign_state_text(r->data.signStatus), 6); printf(" | ");
            print_table_cell_utf8(rental_state_text(r->data.status), 6); printf(" |\n");
            shown++;
            if (!ui_page_break_if_needed(shown)) break;
        }
        printf("+------+--------+---+---+--------+--------+\n");
    } else {
        printf("+------+---+\n");
        printf("| "); print_table_cell_utf8("ID", 4); printf(" | ");
        print_table_cell_utf8("状态", 1); printf(" |\n");
        printf("+------+---+\n");
        for (r = g_db.rentals; r; r = r->next) {
            char idBuf[16];
            snprintf(idBuf, sizeof(idBuf), "%d", r->data.id);
            printf("| "); print_table_cell_utf8(idBuf, 4); printf(" | ");
            const char *status = rental_state_text(r->data.status);
            printf("%c", status[0]); printf(" |\n");
            shown++;
            if (!ui_page_break_if_needed(shown)) break;
        }
        printf("+------+---+\n");
    }
}

static void list_rentals_admin_filtered(void) {
    printf("签约状态筛选: 0.全部  1.待签  2.已签  3.拒签  4.撤销\n");
    int filter = input_int("请选择签约状态编号: ", 0, 4);
    RentalNode *r;
    int cnt = 0;
    if (filter == 0) {
        list_rentals_all();
        return;
    }
    ui_section("租约列表(签约状态筛选)");
    for (r = g_db.rentals; r; r = r->next) {
        if (filter == 1 && r->data.signStatus != RENTAL_SIGN_PENDING) continue;
        if (filter == 2 && r->data.signStatus != RENTAL_SIGN_CONFIRMED) continue;
        if (filter == 3 && r->data.signStatus != RENTAL_SIGN_REJECTED) continue;
        if (filter == 4 && r->data.signStatus != RENTAL_SIGN_CANCELLED) continue;
        print_rental_detailed(&r->data);
        cnt++;
    }
    if (!cnt) printf("无匹配租约。\n");
}

static void query_rentals_by_contract_range(void) {
    char start[11], end[11];
    RentalNode *r;
    int cnt = 0;
    while (1) {
        input_non_empty("签约开始日期(YYYY-MM-DD): ", start, sizeof(start));
        if (validate_date(start)) break;
        printf("日期格式错误。\n");
    }
    while (1) {
        input_non_empty("签约结束日期(YYYY-MM-DD): ", end, sizeof(end));
        if (validate_date(end)) break;
        printf("日期格式错误。\n");
    }
    if (compare_date_str(end, start) < 0) {
        printf("结束日期不能早于开始日期。\n");
        return;
    }
    for (r = g_db.rentals; r; r = r->next) {
        if (compare_date_str(r->data.contractDate, start) < 0) continue;
        if (compare_date_str(r->data.contractDate, end) > 0) continue;
        print_rental_detailed(&r->data);
        cnt++;
    }
    printf("查询完成，共%d条。\n", cnt);
}

static void add_agent_item(void) {
    Agent a;
    memset(&a, 0, sizeof(a));
    a.id = input_int("输入中介ID(1000-4999): ", 1000, 4999);
    if (id_exists_any(a.id)) {
        printf("ID重复。\n");
        return;
    }
    input_non_empty("姓名: ", a.name, sizeof(a.name));
    while (1) {
        input_non_empty("电话: ", a.phone, sizeof(a.phone));
        if (!validate_phone(a.phone)) {
            printf("电话格式错误。\n");
            continue;
        }
        if (find_agent_by_phone(a.phone)) {
            printf("电话已存在，不能重复。\n");
            continue;
        }
        break;
    }
    while (1) {
        input_non_empty("身份证(18位): ", a.idCard, sizeof(a.idCard));
        if (validate_id_card(a.idCard)) break;
        printf("身份证格式错误。\n");
    }
    strcpy(a.password, DEFAULT_AGENT_PASSWORD);
    if (!append_agent(&a)) {
        printf("内存不足。\n");
        return;
    }
    printf("新增成功。\n");
}

/* 按 ID / 电话 / 姓名交互式定位唯一中介。
 * 姓名重名时列出候选并要求通过 ID 或电话二次确认。
 * 返回 NULL 表示未找到或用户放弃。
 */
static AgentNode *locate_agent_interactively(void) {
    printf("查找方式: 1.ID  2.电话  3.姓名\n");
    int mode = input_int("请选择查找方式编号: ", 1, 3);
    if (mode == 1) {
        int id = input_int("中介ID: ", 1000, 4999);
        AgentNode *a = find_agent(id);
        if (!a) printf("中介不存在。\n");
        return a;
    }
    if (mode == 2) {
        char phone[20];
        AgentNode *a;
        input_non_empty("电话: ", phone, sizeof(phone));
        a = find_agent_by_phone(phone);
        if (!a) printf("中介不存在。\n");
        return a;
    }
    /* mode == 3: 按姓名查找，可能重名 */
    {
        char name[MAX_STR];
        AgentNode *cur;
        int cnt = 0;
        input_non_empty("姓名: ", name, sizeof(name));
        for (cur = g_db.agents; cur; cur = cur->next) {
            if (strcmp(cur->data.name, name) == 0) {
                printf("  ID:%-6d 姓名:%-16s 电话:%s\n",
                       cur->data.id, cur->data.name, cur->data.phone);
                cnt++;
            }
        }
        if (cnt == 0) { printf("未找到该姓名的中介。\n"); return NULL; }
        if (cnt == 1) {
            for (cur = g_db.agents; cur; cur = cur->next)
                if (strcmp(cur->data.name, name) == 0) return cur;
            return NULL;
        }
        /* 重名：需二次确认 */
        {
            printf("存在重名，请进一步确认: 1.ID  2.电话\n");
            int sub = input_int("请选择确认方式编号: ", 1, 2);
            if (sub == 1) {
                int confId = input_int("中介ID: ", 1000, 4999);
                AgentNode *found = find_agent(confId);
                if (!found || strcmp(found->data.name, name) != 0) {
                    printf("该ID不在同名名单中。\n");
                    return NULL;
                }
                return found;
            } else {
                char phone[20];
                AgentNode *found;
                input_non_empty("电话: ", phone, sizeof(phone));
                found = find_agent_by_phone(phone);
                if (!found || strcmp(found->data.name, name) != 0) {
                    printf("该电话不在同名名单中。\n");
                    return NULL;
                }
                return found;
            }
        }
    }
}

static void delete_agent_item(void) {
    AgentNode *a = locate_agent_interactively();
    ViewingNode *v;
    RentalNode *r;
    int linked = 0;
    int srcId;
    if (!a) return;
    printf("目标中介: ID:%d 姓名:%s 电话:%s\n", a->data.id, a->data.name, a->data.phone);
    srcId = a->data.id;
    for (v = g_db.viewings; v; v = v->next) if (v->data.agentId == srcId) linked = 1;
    for (r = g_db.rentals; r; r = r->next) if (r->data.agentId == srcId) linked = 1;
    if (linked) {
        if (input_yes_no("存在关联记录，是否转移到其他中介?")) {
            int toId = input_int("目标中介ID: ", 1000, 4999);
            if (!find_agent(toId) || toId == srcId) {
                printf("目标中介无效。\n");
                return;
            }
            for (v = g_db.viewings; v; v = v->next) if (v->data.agentId == srcId) v->data.agentId = toId;
            for (r = g_db.rentals; r; r = r->next) if (r->data.agentId == srcId) r->data.agentId = toId;
        } else {
            printf("取消删除。\n");
            return;
        }
    }
    if (!input_yes_no("确认删除?")) return;
    remove_agent_node(srcId);
    printf("删除成功。\n");
}

static void update_agent_item(void) {
    AgentNode *a = locate_agent_interactively();
    char buf[MAX_STR];
    char masked[32];
    if (!a) return;
    mask_id_card(a->data.idCard, masked, sizeof(masked));
    printf("当前: ID:%d 姓名:%s 电话:%s 身份证:%s\n", a->data.id, a->data.name, a->data.phone, masked);
    printf("新姓名(回车保持): ");
    read_line(buf, sizeof(buf));
    if (buf[0]) {
        strncpy(a->data.name, buf, sizeof(a->data.name) - 1);
        a->data.name[sizeof(a->data.name) - 1] = '\0';
    }
    while (1) {
        printf("新电话(回车保持): ");
        read_line(buf, sizeof(buf));
        if (!buf[0]) break;
        if (!validate_phone(buf)) {
            printf("电话格式错误。\n");
            continue;
        }
        {
            AgentNode *dup = find_agent_by_phone(buf);
            if (dup && dup->data.id != a->data.id) {
                printf("电话已存在，不能重复。\n");
                continue;
            }
        }
        strncpy(a->data.phone, buf, sizeof(a->data.phone) - 1);
        a->data.phone[sizeof(a->data.phone) - 1] = '\0';
        break;
    }
    while (1) {
        printf("新身份证18位(回车保持): ");
        read_line(buf, sizeof(buf));
        if (!buf[0]) break;
        if (!validate_id_card(buf)) {
            printf("身份证格式错误。\n");
            continue;
        }
        strncpy(a->data.idCard, buf, sizeof(a->data.idCard) - 1);
        a->data.idCard[sizeof(a->data.idCard) - 1] = '\0';
        break;
    }
    printf("修改成功。\n");
}

static void query_agent_items(void) {
    printf("查询属性: 1.ID  2.姓名关键字  3.全部  4.综合关键字(姓名/电话/身份证/ID)\n");
    int mode = input_int("请选择查询属性编号: ", 1, 4);
    if (mode == 1) {
        int id = input_int("中介ID: ", 1000, 4999);
        AgentNode *a = find_agent(id);
        if (!a) printf("未找到。\n");
        else {
            char masked[32];
            mask_id_card(a->data.idCard, masked, sizeof(masked));
            printf("ID:%d 姓名:%s 电话:%s 身份证:%s\n", a->data.id, a->data.name, a->data.phone, masked);
        }
    } else if (mode == 2) {
        char kw[MAX_STR];
        AgentNode *a;
        int cnt = 0;
        input_non_empty("姓名关键字: ", kw, sizeof(kw));
        for (a = g_db.agents; a; a = a->next) {
            if (contains_case_insensitive(a->data.name, kw)) {
                char masked[32];
                mask_id_card(a->data.idCard, masked, sizeof(masked));
                printf("ID:%d 姓名:%s 电话:%s 身份证:%s\n", a->data.id, a->data.name, a->data.phone, masked);
                cnt++;
            }
        }
        if (!cnt) printf("未找到。\n");
    } else if (mode == 3) {
        list_agents();
    } else {
        char kw[MAX_STR];
        AgentNode *a;
        int cnt = 0;
        input_non_empty("关键字: ", kw, sizeof(kw));
        for (a = g_db.agents; a; a = a->next) {
            char idBuf[32];
            snprintf(idBuf, sizeof(idBuf), "%d", a->data.id);
            if (!contains_case_insensitive(a->data.name, kw) &&
                !contains_case_insensitive(a->data.phone, kw) &&
                !contains_case_insensitive(a->data.idCard, kw) &&
                !contains_case_insensitive(idBuf, kw)) {
                continue;
            }
            {
                char masked[32];
                mask_id_card(a->data.idCard, masked, sizeof(masked));
                printf("ID:%d 姓名:%s 电话:%s 身份证:%s\n", a->data.id, a->data.name, a->data.phone, masked);
            }
            cnt++;
        }
        if (!cnt) printf("未找到。\n");
    }
}

static void reset_agent_password_by_admin(void) {
    int id = input_int("中介ID: ", 1000, 4999);
    AgentNode *a = find_agent(id);
    if (!a) {
        printf("中介不存在。\n");
        return;
    }
    strcpy(a->data.password, DEFAULT_AGENT_PASSWORD);
    printf("已重置为默认密码。\n");
}

static void query_tenant_items(void) {
    printf("查询属性: 1.ID  2.姓名关键字  3.电话  4.全部  5.综合关键字(姓名/电话/身份证/ID)\n");
    int mode = input_int("请选择查询属性编号: ", 1, 5);
    TenantNode *t;
    int cnt = 0;
    if (mode == 1) {
        int id = input_int("租客ID: ", TENANT_ID_MIN, TENANT_ID_MAX);
        char masked[32];
        t = find_tenant(id);
        if (!t) printf("未找到。\n");
        else {
            mask_id_card(t->data.idCard, masked, sizeof(masked));
            printf("ID:%d 姓名:%s 电话:%s 身份证:%s\n", t->data.id, t->data.name, t->data.phone, masked);
        }
        return;
    }
    if (mode == 2) {
        char kw[MAX_STR];
        input_non_empty("姓名关键字: ", kw, sizeof(kw));
        for (t = g_db.tenants; t; t = t->next) {
            if (contains_case_insensitive(t->data.name, kw)) {
                char masked[32];
                mask_id_card(t->data.idCard, masked, sizeof(masked));
                printf("ID:%d 姓名:%s 电话:%s 身份证:%s\n", t->data.id, t->data.name, t->data.phone, masked);
                cnt++;
            }
        }
    } else if (mode == 3) {
        char phone[20];
        input_non_empty("电话: ", phone, sizeof(phone));
        for (t = g_db.tenants; t; t = t->next) {
            if (strcmp(t->data.phone, phone) == 0) {
                char masked[32];
                mask_id_card(t->data.idCard, masked, sizeof(masked));
                printf("ID:%d 姓名:%s 电话:%s 身份证:%s\n", t->data.id, t->data.name, t->data.phone, masked);
                cnt++;
            }
        }
    } else if (mode == 4) {
        list_tenants();
        return;
    } else {
        char kw[MAX_STR];
        input_non_empty("关键字: ", kw, sizeof(kw));
        for (t = g_db.tenants; t; t = t->next) {
            char idBuf[32];
            snprintf(idBuf, sizeof(idBuf), "%d", t->data.id);
            if (!contains_case_insensitive(t->data.name, kw) &&
                !contains_case_insensitive(t->data.phone, kw) &&
                !contains_case_insensitive(t->data.idCard, kw) &&
                !contains_case_insensitive(idBuf, kw)) {
                continue;
            }
            {
                char masked[32];
                mask_id_card(t->data.idCard, masked, sizeof(masked));
                printf("ID:%d 姓名:%s 电话:%s 身份证:%s\n", t->data.id, t->data.name, t->data.phone, masked);
            }
            cnt++;
        }
    }
    if (!cnt) printf("未找到。\n");
}

static void update_tenant_item(void) {
    int id = input_int("租客ID: ", TENANT_ID_MIN, TENANT_ID_MAX);
    TenantNode *t = find_tenant(id);
    char buf[MAX_STR];
    if (!t) {
        printf("租客不存在。\n");
        return;
    }
    printf("当前姓名:%s 电话:%s\n", t->data.name, t->data.phone);
    printf("新姓名(回车保持): ");
    read_line(buf, sizeof(buf));
    if (buf[0]) {
        strncpy(t->data.name, buf, sizeof(t->data.name) - 1);
        t->data.name[sizeof(t->data.name) - 1] = '\0';
    }
    while (1) {
        printf("新电话(回车保持): ");
        read_line(buf, sizeof(buf));
        if (!buf[0]) break;
        if (!validate_phone(buf)) {
            printf("电话格式错误。\n");
            continue;
        }
        {
            TenantNode *dup = find_tenant_by_phone(buf);
            if (dup && dup->data.id != t->data.id) {
                printf("电话已存在，不能重复。\n");
                continue;
            }
        }
        strncpy(t->data.phone, buf, sizeof(t->data.phone) - 1);
        t->data.phone[sizeof(t->data.phone) - 1] = '\0';
        break;
    }
    while (1) {
        printf("新身份证18位(回车保持): ");
        read_line(buf, sizeof(buf));
        if (!buf[0]) break;
        if (!validate_id_card(buf)) {
            printf("身份证格式错误。\n");
            continue;
        }
        strncpy(t->data.idCard, buf, sizeof(t->data.idCard) - 1);
        t->data.idCard[sizeof(t->data.idCard) - 1] = '\0';
        break;
    }
    printf("修改成功。\n");
}

static void reset_tenant_password_by_admin(void) {
    int id = input_int("租客ID: ", TENANT_ID_MIN, TENANT_ID_MAX);
    TenantNode *t = find_tenant(id);
    if (!t) {
        printf("租客不存在。\n");
        return;
    }
    strcpy(t->data.password, DEFAULT_TENANT_PASSWORD);
    printf("已重置为默认密码。\n");
}

static void delete_tenant_item(void) {
    int id = input_int("租客ID: ", TENANT_ID_MIN, TENANT_ID_MAX);
    ViewingNode *v;
    RentalNode *r;
    int linked = 0;
    if (!find_tenant(id)) {
        printf("租客不存在。\n");
        return;
    }
    for (v = g_db.viewings; v; v = v->next) if (v->data.tenantId == id) linked = 1;
    for (r = g_db.rentals; r; r = r->next) if (r->data.tenantId == id) linked = 1;
    if (linked) {
        printf("租客存在关联看房/租约，不允许直接删除。\n");
        return;
    }
    if (!input_yes_no("确认删除租客?")) return;
    remove_tenant_node(id);
    printf("删除成功。\n");
}

static void update_house_item(void) {
    int id = input_int("房源ID: ", 1, 99999999);
    HouseNode *h = find_house(id);
    char buf[MAX_BIG_STR];
    if (!h) {
        printf("房源不存在。\n");
        return;
    }
    print_house_detailed(&h->data);
    printf("新城市(回车保持): ");
    read_line(buf, sizeof(buf));
    if (buf[0]) {
        strncpy(h->data.city, buf, sizeof(h->data.city) - 1);
        h->data.city[sizeof(h->data.city) - 1] = '\0';
    }
    printf("是否重新选择区域?\n");
    if (input_yes_no("选择")) category_pick(&g_db.regions, "区域", h->data.region);
    printf("新小区(回车保持): ");
    read_line(buf, sizeof(buf));
    if (buf[0]) {
        strncpy(h->data.community, buf, sizeof(h->data.community) - 1);
        h->data.community[sizeof(h->data.community) - 1] = '\0';
    }
    printf("路/街道地址(回车保持,当前:%s): ", h->data.address);
    read_line(buf, sizeof(buf));
    if (buf[0]) {
        strncpy(h->data.address, buf, sizeof(h->data.address) - 1);
        h->data.address[sizeof(h->data.address) - 1] = '\0';
    }
    printf("楼栋(回车保持,当前:%s): ", h->data.building);
    read_line(buf, sizeof(buf));
    if (buf[0]) {
        strncpy(h->data.building, buf, sizeof(h->data.building) - 1);
        h->data.building[sizeof(h->data.building) - 1] = '\0';
    }
    if (input_yes_no("修改层数?")) h->data.floor = input_int("第几层: ", 1, 200);
    if (input_yes_no("修改楼层说明?")) category_pick(&g_db.floorNotes, "楼层说明", h->data.floorNote);
    printf("室号(回车保持,当前:%s): ", h->data.unitNo);
    read_line(buf, sizeof(buf));
    if (buf[0]) {
        strncpy(h->data.unitNo, buf, sizeof(h->data.unitNo) - 1);
        h->data.unitNo[sizeof(h->data.unitNo) - 1] = '\0';
    }
    if (input_yes_no("修改朝向?")) category_pick(&g_db.orientations, "朝向", h->data.orientation);
    if (input_yes_no("修改户型?")) category_pick(&g_db.houseTypes, "户型", h->data.houseType);
    if (input_yes_no("修改面积?")) h->data.area = input_double("面积: ", 0.01, 100000.0);
    if (input_yes_no("修改装修?")) category_pick(&g_db.decorations, "装修", h->data.decoration);
    if (input_yes_no("修改挂牌租金?")) h->data.price = input_double("挂牌租金: ", 0.01, 10000000.0);
    if (input_yes_no("修改状态?")) {
        printf("房源状态可选: 0.空闲  1.已出租  2.待审核  3.下架\n");
        h->data.status = input_int("请选择房源状态编号: ", 0, 3);
    }
    printf("修改成功。\n");
}

static void delete_house_item(void) {
    int id = input_int("房源ID: ", 1, 99999999);
    ViewingNode *v;
    RentalNode *r;
    if (!find_house(id)) {
        printf("房源不存在。\n");
        return;
    }
    for (v = g_db.viewings; v; v = v->next) {
        if (v->data.houseId == id) {
            printf("存在关联看房记录，不允许删除。\n");
            return;
        }
    }
    for (r = g_db.rentals; r; r = r->next) {
        if (r->data.houseId == id) {
            printf("存在关联租约记录，不允许删除。\n");
            return;
        }
    }
    if (!input_yes_no("确认删除房源?")) return;
    remove_house_node(id);
    printf("删除成功。\n");
}

static void audit_pending_houses(void) {
    while (1) {
        HouseNode *h;
        int found = 0;
        int targetId;
        int op;

        if (!reload_database_for_sync()) return;

        ui_section("待审核房源(已从文件刷新)");
        for (h = g_db.houses; h; h = h->next) {
            if (h->data.status != HOUSE_PENDING) continue;
            found = 1;
            print_house_detailed(&h->data);
        }
        if (!found) {
            printf("暂无待审核房源。\n");
            return;
        }

        printf("输入房源ID进行审核，0返回，-1刷新: ");
        {
            char buf[64], *end;
            long v;
            read_line(buf, sizeof(buf));
            v = strtol(buf, &end, 10);
            if (end == buf || *end != '\0') {
                printf("输入无效。\n");
                continue;
            }
            if (v == 0) return;
            if (v == -1) continue;
            if (v < 1 || v > 99999999) {
                printf("房源ID越界。\n");
                continue;
            }
            targetId = (int)v;
        }

        h = find_house(targetId);
        if (!h || h->data.status != HOUSE_PENDING) {
            printf("该房源不存在或已不是待审核状态。\n");
            continue;
        }

        print_house_detailed(&h->data);
        op = input_int("1通过(空闲) 2驳回(下架) 3取消: ", 1, 3);
        if (op == 1) {
            h->data.status = HOUSE_VACANT;
            h->data.rejectReason[0] = '\0';
            autosave_default();
            printf("审核通过。\n");
        } else if (op == 2) {
            input_non_empty("驳回原因: ", h->data.rejectReason, sizeof(h->data.rejectReason));
            h->data.status = HOUSE_OFFLINE;
            autosave_default();
            printf("已驳回并保存。\n");
        }
    }
}

static void update_rental_status_admin(void) {
    int id = input_int("租房ID: ", 1, 99999999);
    RentalNode *r = find_rental(id);
    if (!r) {
        printf("记录不存在。\n");
        return;
    }
    if (r->data.signStatus != RENTAL_SIGN_CONFIRMED) {
        printf("该合同尚未签订，不能修改履约状态。\n");
        return;
    }
    printf("履约状态可选: 0.有效  1.已到期  2.提前退租\n");
    r->data.status = input_int("请选择履约状态编号: ", 0, 2);
    refresh_house_status(r->data.houseId);
    printf("更新成功。\n");
}

static void add_house_item(void) {
    House h;
    h.id = input_int("房源ID: ", 1, 99999999);
    if (id_exists_any(h.id)) {
        printf("ID重复。\n");
        return;
    }
    input_non_empty("城市: ", h.city, sizeof(h.city));
    if (!category_pick(&g_db.regions, "区域", h.region)) return;
    input_non_empty("小区: ", h.community, sizeof(h.community));
    input_non_empty("路/街道地址: ", h.address, sizeof(h.address));
    input_non_empty("楼栋(如 1栋/A座): ", h.building, sizeof(h.building));
    h.floor = input_int("第几层: ", 1, 200);
    if (!category_pick(&g_db.floorNotes, "楼层说明", h.floorNote)) return;
    input_non_empty("室号(如 1201室): ", h.unitNo, sizeof(h.unitNo));
    if (!category_pick(&g_db.orientations, "朝向", h.orientation)) return;
    if (!category_pick(&g_db.houseTypes, "房型", h.houseType)) return;
    h.area = input_double("面积: ", 0.01, 100000.0);
    if (!category_pick(&g_db.decorations, "装修", h.decoration)) return;
    h.price = input_double("挂牌租金: ", 0.01, 10000000.0);
    input_non_empty("房主姓名: ", h.ownerName, sizeof(h.ownerName));
    while (1) {
        input_non_empty("房主电话: ", h.ownerPhone, sizeof(h.ownerPhone));
        if (validate_phone(h.ownerPhone)) break;
        printf("电话格式错误。\n");
    }
    h.createdByAgentId = -1;
    h.rejectReason[0] = '\0';
    h.status = HOUSE_VACANT;
    if (!append_house(&h)) {
        printf("内存不足。\n");
        return;
    }
    printf("新增成功，已直接上架(免审)。\n");
}

static void add_house_item_for_agent(int agentId) {
    House h;
    h.id = input_int("房源ID: ", 1, 99999999);
    if (id_exists_any(h.id)) {
        printf("ID重复。\n");
        return;
    }
    input_non_empty("城市: ", h.city, sizeof(h.city));
    if (!category_pick(&g_db.regions, "区域", h.region)) return;
    input_non_empty("小区: ", h.community, sizeof(h.community));
    input_non_empty("路/街道地址: ", h.address, sizeof(h.address));
    input_non_empty("楼栋(如 1栋/A座): ", h.building, sizeof(h.building));
    h.floor = input_int("第几层: ", 1, 200);
    if (!category_pick(&g_db.floorNotes, "楼层说明", h.floorNote)) return;
    input_non_empty("室号(如 1201室): ", h.unitNo, sizeof(h.unitNo));
    if (!category_pick(&g_db.orientations, "朝向", h.orientation)) return;
    if (!category_pick(&g_db.houseTypes, "房型", h.houseType)) return;
    h.area = input_double("面积: ", 0.01, 100000.0);
    if (!category_pick(&g_db.decorations, "装修", h.decoration)) return;
    h.price = input_double("挂牌租金: ", 0.01, 10000000.0);
    input_non_empty("房主姓名: ", h.ownerName, sizeof(h.ownerName));
    while (1) {
        input_non_empty("房主电话: ", h.ownerPhone, sizeof(h.ownerPhone));
        if (validate_phone(h.ownerPhone)) break;
        printf("电话格式错误。\n");
    }
    h.createdByAgentId = agentId;
    h.status = HOUSE_PENDING;
    h.rejectReason[0] = '\0';

    if (!append_house(&h)) {
        printf("内存不足。\n");
        return;
    }
    autosave_default();
    printf("房源已提交审核，等待管理员处理。\n");
}

static void list_submitted_houses_for_agent(int agentId) {
    HouseNode *h;
    int cnt = 0;
    ui_section("我提交的房源");
    for (h = g_db.houses; h; h = h->next) {
        if (h->data.createdByAgentId != agentId) continue;
        print_house_detailed(&h->data);
        cnt++;
    }
    if (!cnt) printf("暂无你提交的房源。\n");
}

static void resubmit_rejected_house_for_agent(int agentId) {
    int id;
    HouseNode *h;
    if (!reload_database_for_sync()) return;
    list_submitted_houses_for_agent(agentId);
    id = input_int("输入要重新提交审核的房源ID(0返回): ", 0, 99999999);
    if (id == 0) return;
    h = find_house(id);
    if (!h || h->data.createdByAgentId != agentId) {
        printf("房源不存在或不属于你提交。\n");
        return;
    }
    if (h->data.status != HOUSE_OFFLINE) {
        printf("仅审核不通过/下架的房源可重新提交。\n");
        return;
    }
    h->data.status = HOUSE_PENDING;
    h->data.rejectReason[0] = '\0';
    autosave_default();
    printf("已重新提交审核。\n");
}

static void agent_submitted_houses_menu(int agentId) {
    int ch;
    while (1) {
        if (!reload_database_for_sync()) return;
        printf("\n--- 我提交的房源 ---\n");
        printf("1. 查看列表(含审核状态)\n");
        printf("2. 将驳回房源重新提交审核\n");
        printf("0. 返回\n");
        ch = input_int("请选择操作编号: ", 0, 2);
        if (ch == 0) return;
        if (ch == 1) list_submitted_houses_for_agent(agentId);
        else resubmit_rejected_house_for_agent(agentId);
    }
}

static void add_viewing_for_tenant(int tenantId) {
    if (!find_tenant(tenantId)) {
        printf("租客不存在。\n");
        return;
    }
    search_houses_for_tenant(tenantId);
}

static void add_rental_for_agent(int agentId) {
    Rental r;
    HouseNode *h;

    r.id = input_int("租房ID: ", 1, 99999999);
    if (id_exists_any(r.id)) {
        printf("ID重复。\n");
        return;
    }

    r.houseId = input_int("房源ID: ", 1, 99999999);
    h = find_house(r.houseId);
    if (!h) {
        printf("房源不存在。\n");
        return;
    }
    if (h->data.status != HOUSE_VACANT) {
        printf("房源非空闲，不可签约。\n");
        return;
    }
    if (has_open_contract_for_house(r.houseId, -1)) {
        printf("该房源已有待签或生效中的租约，不能重复发起。\n");
        return;
    }

    r.tenantId = input_int("租客ID: ", TENANT_ID_MIN, TENANT_ID_MAX);
    if (!find_tenant(r.tenantId)) {
        printf("租客不存在。\n");
        return;
    }
    r.agentId = agentId;

    while (1) {
        input_non_empty("合同日期(YYYY-MM-DD): ", r.contractDate, sizeof(r.contractDate));
        if (validate_date(r.contractDate)) break;
        printf("日期格式错误。\n");
    }
    while (1) {
        input_non_empty("起租日期(YYYY-MM-DD): ", r.startDate, sizeof(r.startDate));
        if (validate_date(r.startDate)) break;
        printf("日期格式错误。\n");
    }
    while (1) {
        input_non_empty("到期日期(YYYY-MM-DD): ", r.endDate, sizeof(r.endDate));
        if (validate_date(r.endDate)) break;
        printf("日期格式错误。\n");
    }

    if (compare_date_str(r.startDate, r.contractDate) < 0) {
        printf("起租日期不能早于合同日期。\n");
        return;
    }
    if (compare_date_str(r.endDate, r.startDate) <= 0) {
        printf("到期日期必须晚于起租日期。\n");
        return;
    }

    r.monthlyRent = input_double("实际月租: ", 0.01, 10000000.0);
    r.status = RENTAL_ACTIVE;
    r.signStatus = RENTAL_SIGN_PENDING;

    if (!append_rental(&r)) {
        printf("内存不足。\n");
        return;
    }
    printf("合同已发起，等待租客确认签订。\n");
}

static void assign_viewings_admin(void) {
    ViewingNode *v;
    int found = 0;
    for (v = g_db.viewings; v; v = v->next) {
        int aid;
        if (v->data.agentId != 0) continue;
        found = 1;
        print_viewing_detailed(&v->data);
        aid = input_int("分配中介ID(0跳过): ", 0, 4999);
        if (aid == 0) continue;
        if (!find_agent(aid)) {
            printf("中介不存在，跳过该条。\n");
            continue;
        }
        if (viewing_conflict(v->data.datetime, v->data.durationMinutes, v->data.houseId, aid, v->data.id)) {
            printf("分配冲突，跳过该条。\n");
            continue;
        }
        v->data.agentId = aid;
        printf("分配成功。\n");
    }
    if (!found) printf("暂无待分配看房记录。\n");
}

static void list_agent_viewings(int agentId) {
    ViewingNode *v;
    int cnt = 0;
    ui_section("我的看房");
    for (v = g_db.viewings; v; v = v->next) {
        if (v->data.agentId == agentId) {
            print_viewing_detailed(&v->data);
            cnt++;
        }
    }
    if (!cnt) printf("暂无记录。\n");
}

static void process_pending_viewings_for_agent(int agentId) {
    while (1) {
        ViewingNode *v;
        int found = 0;
        int targetId;
        int op;

        if (!reload_database_for_sync()) return;

        ui_section("待处理看房预约(已同步)");
        for (v = g_db.viewings; v; v = v->next) {
            if (v->data.agentId != agentId || v->data.status != VIEWING_UNCONFIRMED) continue;
            print_viewing_detailed(&v->data);
            found = 1;
        }
        if (!found) {
            printf("暂无待处理预约。\n");
            return;
        }

        printf("输入看房ID处理，0返回，-1刷新: ");
        {
            char buf[64], *end;
            long val;
            read_line(buf, sizeof(buf));
            val = strtol(buf, &end, 10);
            if (end == buf || *end != '\0') {
                printf("输入无效。\n");
                continue;
            }
            if (val == 0) return;
            if (val == -1) continue;
            if (val < 1 || val > 99999999) {
                printf("ID越界。\n");
                continue;
            }
            targetId = (int)val;
        }

        v = find_viewing(targetId);
        if (!v || v->data.agentId != agentId || v->data.status != VIEWING_UNCONFIRMED) {
            printf("记录不存在或已被处理。\n");
            continue;
        }

        print_viewing_detailed(&v->data);
        op = input_int("1同意 2拒绝(必须理由) 3取消: ", 1, 3);
        if (op == 1) {
            char note[MAX_BIG_STR];
            char now[20];
            printf("同意备注(可空): ");
            read_line(note, sizeof(note));
            get_current_datetime(now, sizeof(now));
            v->data.status = VIEWING_CONFIRMED;
            format_viewing_feedback(v->data.agentFeedback, sizeof(v->data.agentFeedback), "同意", now, note);
            autosave_default();
            printf("已同意预约。\n");
        } else if (op == 2) {
            char reason[MAX_BIG_STR];
            char now[20];
            input_non_empty("拒绝理由: ", reason, sizeof(reason));
            get_current_datetime(now, sizeof(now));
            v->data.status = VIEWING_CANCELLED;
            format_viewing_feedback(v->data.agentFeedback, sizeof(v->data.agentFeedback), "拒绝", now, reason);
            autosave_default();
            printf("已拒绝预约。\n");
        }
    }
}

static void delete_viewing_for_agent(int agentId) {
    int id = input_int("看房ID: ", 1, 99999999);
    ViewingNode *v = find_viewing(id);
    if (!v || v->data.agentId != agentId) {
        printf("记录不存在或无权限。\n");
        return;
    }
    if (!input_yes_no("确认删除该看房记录?")) return;
    remove_viewing(id);
    printf("删除成功。\n");
}

static void query_agent_viewings(int agentId) {
    printf("查询属性: 1.看房ID  2.租客ID  3.房源ID  4.状态  5.时间范围  6.全部  7.关键字模糊\n");
    int mode = input_int("请选择查询属性编号: ", 1, 7);
    ViewingNode *v;
    int cnt = 0;
    if (mode == 1) {
        int id = input_int("看房ID: ", 1, 99999999);
        v = find_viewing(id);
        if (!v || v->data.agentId != agentId) {
            printf("未找到。\n");
            return;
        }
        print_viewing_detailed(&v->data);
        return;
    }
    if (mode == 2) {
        int tenantId = input_int("租客ID: ", TENANT_ID_MIN, TENANT_ID_MAX);
        for (v = g_db.viewings; v; v = v->next) {
            if (v->data.agentId == agentId && v->data.tenantId == tenantId) {
                print_viewing_detailed(&v->data);
                cnt++;
            }
        }
    } else if (mode == 3) {
        int houseId = input_int("房源ID: ", 1, 99999999);
        for (v = g_db.viewings; v; v = v->next) {
            if (v->data.agentId == agentId && v->data.houseId == houseId) {
                print_viewing_detailed(&v->data);
                cnt++;
            }
        }
    } else if (mode == 4) {
        printf("看房状态可选: 0.待确认  1.已确认  2.已完成  3.已取消  4.未赴约\n");
        int st = input_int("请选择状态编号: ", 0, 4);
        for (v = g_db.viewings; v; v = v->next) {
            if (v->data.agentId == agentId && v->data.status == st) {
                print_viewing_detailed(&v->data);
                cnt++;
            }
        }
    } else if (mode == 5) {
        char start[20], end[20];
        while (1) {
            input_non_empty("开始时间(YYYY-MM-DD HH:MM): ", start, sizeof(start));
            if (validate_datetime(start)) break;
            printf("时间格式错误。\n");
        }
        while (1) {
            input_non_empty("结束时间(YYYY-MM-DD HH:MM): ", end, sizeof(end));
            if (validate_datetime(end)) break;
            printf("时间格式错误。\n");
        }
        if (strcmp(end, start) < 0) {
            printf("结束时间不能早于开始时间。\n");
            return;
        }
        for (v = g_db.viewings; v; v = v->next) {
            if (v->data.agentId != agentId) continue;
            if (strcmp(v->data.datetime, start) < 0) continue;
            if (strcmp(v->data.datetime, end) > 0) continue;
            print_viewing_detailed(&v->data);
            cnt++;
        }
    } else if (mode == 6) {
        list_agent_viewings(agentId);
        return;
    } else {
        char kw[MAX_STR];
        input_non_empty("关键字(时间/反馈/状态/ID): ", kw, sizeof(kw));
        for (v = g_db.viewings; v; v = v->next) {
            char idBuf[32], houseBuf[32], tenantBuf[32], agentBuf[32];
            if (v->data.agentId != agentId) continue;
            snprintf(idBuf, sizeof(idBuf), "%d", v->data.id);
            snprintf(houseBuf, sizeof(houseBuf), "%d", v->data.houseId);
            snprintf(tenantBuf, sizeof(tenantBuf), "%d", v->data.tenantId);
            snprintf(agentBuf, sizeof(agentBuf), "%d", v->data.agentId);
            if (!contains_case_insensitive(v->data.datetime, kw) &&
                !contains_case_insensitive(v->data.tenantFeedback, kw) &&
                !contains_case_insensitive(v->data.agentFeedback, kw) &&
                !contains_case_insensitive(viewing_state_text(v->data.status), kw) &&
                !contains_case_insensitive(idBuf, kw) &&
                !contains_case_insensitive(houseBuf, kw) &&
                !contains_case_insensitive(tenantBuf, kw) &&
                !contains_case_insensitive(agentBuf, kw)) {
                continue;
            }
            print_viewing_detailed(&v->data);
            cnt++;
        }
    }
    if (!cnt) printf("未找到。\n");
}

static void query_agent_rentals(int agentId) {
    printf("查询属性: 1.租约ID  2.租客ID  3.房源ID  4.履约状态  5.合同日期范围  6.全部  7.关键字模糊  8.签约状态\n");
    int mode = input_int("请选择查询属性编号: ", 1, 8);
    RentalNode *r;
    int cnt = 0;
    if (mode == 1) {
        int id = input_int("租约ID: ", 1, 99999999);
        r = find_rental(id);
        if (!r || r->data.agentId != agentId) {
            printf("未找到。\n");
            return;
        }
        print_rental_detailed(&r->data);
        return;
    }
    if (mode == 2) {
        int tenantId = input_int("租客ID: ", TENANT_ID_MIN, TENANT_ID_MAX);
        for (r = g_db.rentals; r; r = r->next) {
            if (r->data.agentId == agentId && r->data.tenantId == tenantId) {
                print_rental_detailed(&r->data);
                cnt++;
            }
        }
    } else if (mode == 3) {
        int houseId = input_int("房源ID: ", 1, 99999999);
        for (r = g_db.rentals; r; r = r->next) {
            if (r->data.agentId == agentId && r->data.houseId == houseId) {
                print_rental_detailed(&r->data);
                cnt++;
            }
        }
    } else if (mode == 4) {
        printf("履约状态可选: 0.有效  1.已到期  2.提前退租\n");
        int st = input_int("请选择履约状态编号: ", 0, 2);
        for (r = g_db.rentals; r; r = r->next) {
            if (r->data.agentId == agentId && r->data.status == st) {
                print_rental_detailed(&r->data);
                cnt++;
            }
        }
    } else if (mode == 5) {
        char start[11], end[11];
        while (1) {
            input_non_empty("签约开始日期(YYYY-MM-DD): ", start, sizeof(start));
            if (validate_date(start)) break;
            printf("日期格式错误。\n");
        }
        while (1) {
            input_non_empty("签约结束日期(YYYY-MM-DD): ", end, sizeof(end));
            if (validate_date(end)) break;
            printf("日期格式错误。\n");
        }
        if (compare_date_str(end, start) < 0) {
            printf("结束日期不能早于开始日期。\n");
            return;
        }
        for (r = g_db.rentals; r; r = r->next) {
            if (r->data.agentId != agentId) continue;
            if (compare_date_str(r->data.contractDate, start) < 0) continue;
            if (compare_date_str(r->data.contractDate, end) > 0) continue;
            print_rental_detailed(&r->data);
            cnt++;
        }
    } else if (mode == 6) {
        RentalNode *cur;
        ui_section("我的租约");
        for (cur = g_db.rentals; cur; cur = cur->next) {
            if (cur->data.agentId == agentId) print_rental_detailed(&cur->data);
        }
        return;
    } else if (mode == 7) {
        char kw[MAX_STR];
        input_non_empty("关键字(日期/状态/ID): ", kw, sizeof(kw));
        for (r = g_db.rentals; r; r = r->next) {
            char idBuf[32], houseBuf[32], tenantBuf[32], agentBuf[32];
            if (r->data.agentId != agentId) continue;
            snprintf(idBuf, sizeof(idBuf), "%d", r->data.id);
            snprintf(houseBuf, sizeof(houseBuf), "%d", r->data.houseId);
            snprintf(tenantBuf, sizeof(tenantBuf), "%d", r->data.tenantId);
            snprintf(agentBuf, sizeof(agentBuf), "%d", r->data.agentId);
            if (!contains_case_insensitive(r->data.contractDate, kw) &&
                !contains_case_insensitive(r->data.startDate, kw) &&
                !contains_case_insensitive(r->data.endDate, kw) &&
                !contains_case_insensitive(rental_sign_state_text(r->data.signStatus), kw) &&
                !contains_case_insensitive(rental_state_text(r->data.status), kw) &&
                !contains_case_insensitive(idBuf, kw) &&
                !contains_case_insensitive(houseBuf, kw) &&
                !contains_case_insensitive(tenantBuf, kw) &&
                !contains_case_insensitive(agentBuf, kw)) {
                continue;
            }
            print_rental_detailed(&r->data);
            cnt++;
        }
    } else {
        printf("签约状态可选: 0.待签  1.已签  2.拒签  3.撤销\n");
        int signSt = input_int("请选择签约状态编号: ", 0, 3);
        for (r = g_db.rentals; r; r = r->next) {
            if (r->data.agentId == agentId && r->data.signStatus == signSt) {
                print_rental_detailed(&r->data);
                cnt++;
            }
        }
    }
    if (!cnt) printf("未找到。\n");
}

static void sort_agent_viewings(int agentId) {
    printf("排序属性: 1.时间  2.时长  3.状态(同状态再按时间)\n");
    int mode = input_int("请选择排序属性编号: ", 1, 3);
    int ord = input_int("1升序 2降序: ", 1, 2);
    Viewing *arr;
    int n = 0;
    int i = 0, j;
    ViewingNode *v;

    for (v = g_db.viewings; v; v = v->next) if (v->data.agentId == agentId) n++;
    if (n == 0) {
        printf("暂无看房记录。\n");
        return;
    }

    arr = (Viewing *)malloc((size_t)n * sizeof(Viewing));
    if (!arr) {
        printf("内存不足。\n");
        return;
    }

    for (v = g_db.viewings; v; v = v->next) {
        if (v->data.agentId == agentId) arr[i++] = v->data;
    }

    for (i = 0; i < n - 1; ++i) {
        for (j = 0; j < n - 1 - i; ++j) {
            int swap = 0;
            if (mode == 1) {
                int c = strcmp(arr[j].datetime, arr[j + 1].datetime);
                swap = (ord == 1) ? (c > 0) : (c < 0);
            } else if (mode == 2) {
                swap = (ord == 1) ? (arr[j].durationMinutes > arr[j + 1].durationMinutes)
                                  : (arr[j].durationMinutes < arr[j + 1].durationMinutes);
            } else {
                int c = arr[j].status - arr[j + 1].status;
                if (c == 0) c = strcmp(arr[j].datetime, arr[j + 1].datetime);
                swap = (ord == 1) ? (c > 0) : (c < 0);
            }
            if (swap) {
                Viewing tmp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = tmp;
            }
        }
    }

    ui_section("看房排序结果");
    for (i = 0; i < n; ++i) print_viewing_detailed(&arr[i]);
    free(arr);
}

static void sort_agent_rentals(int agentId) {
    printf("排序属性: 1.合同日期  2.月租  3.起租日期(同日起再按月租)\n");
    int mode = input_int("请选择排序属性编号: ", 1, 3);
    int ord = input_int("1升序 2降序: ", 1, 2);
    Rental *arr;
    int n = 0;
    int i = 0, j;
    RentalNode *r;

    for (r = g_db.rentals; r; r = r->next) if (r->data.agentId == agentId) n++;
    if (n == 0) {
        printf("暂无租约记录。\n");
        return;
    }

    arr = (Rental *)malloc((size_t)n * sizeof(Rental));
    if (!arr) {
        printf("内存不足。\n");
        return;
    }

    for (r = g_db.rentals; r; r = r->next) {
        if (r->data.agentId == agentId) arr[i++] = r->data;
    }

    for (i = 0; i < n - 1; ++i) {
        for (j = 0; j < n - 1 - i; ++j) {
            int swap = 0;
            if (mode == 1) {
                int c = compare_date_str(arr[j].contractDate, arr[j + 1].contractDate);
                swap = (ord == 1) ? (c > 0) : (c < 0);
            } else if (mode == 2) {
                swap = (ord == 1) ? (arr[j].monthlyRent > arr[j + 1].monthlyRent)
                                  : (arr[j].monthlyRent < arr[j + 1].monthlyRent);
            } else {
                int c = compare_date_str(arr[j].startDate, arr[j + 1].startDate);
                if (c == 0) {
                    if (arr[j].monthlyRent < arr[j + 1].monthlyRent) c = -1;
                    else if (arr[j].monthlyRent > arr[j + 1].monthlyRent) c = 1;
                }
                swap = (ord == 1) ? (c > 0) : (c < 0);
            }
            if (swap) {
                Rental tmp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = tmp;
            }
        }
    }

    ui_section("租约排序结果");
    for (i = 0; i < n; ++i) print_rental_detailed(&arr[i]);
    free(arr);
}

static void agent_query_menu(int agentId) {
    printf("查询菜单: 1.租客看房预约  2.租客租约合同  3.房源  4.房源时段可预约性\n");
    int ch = input_int("请选择查询菜单编号: ", 1, 4);
    if (ch == 1) query_agent_viewings(agentId);
    else if (ch == 2) query_agent_rentals(agentId);
    else if (ch == 3) query_houses_combo();
    else query_house_availability_by_time();
}

static void agent_sort_menu(int agentId) {
    printf("排序菜单: 1.租客看房预约  2.租客租约合同  3.房源\n");
    int ch = input_int("请选择排序菜单编号: ", 1, 3);
    if (ch == 1) sort_agent_viewings(agentId);
    else if (ch == 2) sort_agent_rentals(agentId);
    else sort_houses();
}

static void agent_statistics_menu(int agentId) {
    int ch = input_int("1看房统计 2租约统计 3指定租客业务统计 4按签约月份统计: ", 1, 4);
    if (ch == 1) {
        ViewingNode *v;
        int total = 0;
        int statusCount[5] = {0, 0, 0, 0, 0};
        int durationSum = 0;
        for (v = g_db.viewings; v; v = v->next) {
            if (v->data.agentId != agentId) continue;
            total++;
            durationSum += v->data.durationMinutes;
            if (v->data.status >= 0 && v->data.status <= 4) statusCount[v->data.status]++;
        }
        printf("中介ID:%d 看房总数:%d 平均时长:%.2f分钟\n", agentId, total,
               total ? (double)durationSum / total : 0.0);
        printf("待确认:%d 已确认:%d 已完成:%d 已取消:%d 未赴约:%d\n",
               statusCount[0], statusCount[1], statusCount[2], statusCount[3], statusCount[4]);
    } else if (ch == 2) {
        RentalNode *r;
        int total = 0;
        int active = 0, expired = 0, early = 0;
        double rentSum = 0.0;
        double daysSum = 0.0;
        for (r = g_db.rentals; r; r = r->next) {
            time_t ts, te;
            double days;
            if (r->data.agentId != agentId) continue;
            total++;
            rentSum += r->data.monthlyRent;
            if (r->data.status == RENTAL_ACTIVE) active++;
            else if (r->data.status == RENTAL_EXPIRED) expired++;
            else if (r->data.status == RENTAL_EARLY) early++;
            ts = date_to_time(r->data.startDate);
            te = date_to_time(r->data.endDate);
            if (ts != (time_t)-1 && te != (time_t)-1 && te > ts) {
                days = difftime(te, ts) / 86400.0;
                daysSum += days;
            }
        }
        printf("中介ID:%d 租约总数:%d 有效:%d 到期:%d 提前退租:%d\n",
               agentId, total, active, expired, early);
        printf("月租总额:%.2f 平均月租:%.2f 平均租期:%.2f天\n",
               rentSum, total ? rentSum / total : 0.0, total ? daysSum / total : 0.0);
    } else if (ch == 3) {
        int tenantId = input_int("租客ID: ", TENANT_ID_MIN, TENANT_ID_MAX);
        ViewingNode *v;
        RentalNode *r;
        int vcnt = 0;
        int rcnt = 0;
        double rentSum = 0.0;
        for (v = g_db.viewings; v; v = v->next) {
            if (v->data.agentId == agentId && v->data.tenantId == tenantId) vcnt++;
        }
        for (r = g_db.rentals; r; r = r->next) {
            if (r->data.agentId == agentId && r->data.tenantId == tenantId) {
                rcnt++;
                rentSum += r->data.monthlyRent;
            }
        }
        printf("租客ID:%d 在该中介名下 看房:%d 租约:%d 累计月租:%.2f\n", tenantId, vcnt, rcnt, rentSum);
    } else {
        char month[8];
        RentalNode *r;
        int cnt = 0;
        double rentSum = 0.0;
        while (1) {
            input_non_empty("签约月份(YYYY-MM): ", month, sizeof(month));
            if (strlen(month) == 7 && month[4] == '-') break;
            printf("月份格式错误。\n");
        }
        for (r = g_db.rentals; r; r = r->next) {
            if (r->data.agentId != agentId) continue;
            if (strncmp(r->data.contractDate, month, 7) != 0) continue;
            cnt++;
            rentSum += r->data.monthlyRent;
        }
        printf("月份:%s 签约数:%d 平均月租:%.2f\n", month, cnt, cnt ? rentSum / cnt : 0.0);
    }
}

static void update_viewing_for_agent(int agentId) {
    int id = input_int("看房ID: ", 1, 99999999);
    ViewingNode *v = find_viewing(id);
    char dt[20];
    int dur;
    int st;
    if (!v || v->data.agentId != agentId) {
        printf("记录不存在或无权限。\n");
        return;
    }
    printf("当前时间:%s 时长:%d 状态:%s\n", v->data.datetime, v->data.durationMinutes, viewing_state_text(v->data.status));
    printf("新时间(回车保持): ");
    read_line(dt, sizeof(dt));
    dur = v->data.durationMinutes;
    if (input_yes_no("修改时长?")) dur = input_int("时长(分钟): ", 10, 600);
    if (dt[0] && !validate_datetime(dt)) {
        printf("时间格式错误。\n");
        return;
    }
    if (dt[0] && !appointment_time_is_in_future(dt)) {
        printf("预约时间必须晚于当前时间。\n");
        return;
    }
    if (viewing_conflict(dt[0] ? dt : v->data.datetime, dur, v->data.houseId, v->data.agentId, v->data.id)) {
        printf("时间冲突。\n");
        return;
    }
    if (dt[0]) {
        strncpy(v->data.datetime, dt, sizeof(v->data.datetime) - 1);
        v->data.datetime[sizeof(v->data.datetime) - 1] = '\0';
    }
    v->data.durationMinutes = dur;
    printf("看房状态可选: 0.待确认  1.已确认  2.已完成  3.已取消  4.未赴约\n");
    st = input_int("请选择看房状态编号: ", 0, 4);
    v->data.status = st;
    if (input_yes_no("更新中介反馈?")) {
        input_non_empty("中介反馈: ", v->data.agentFeedback, sizeof(v->data.agentFeedback));
    }
    printf("更新成功。\n");
}

static int tenant_has_modifiable_viewing(int tenantId) {
    ViewingNode *cur;
    for (cur = g_db.viewings; cur; cur = cur->next) {
        if (cur->data.tenantId != tenantId) continue;
        if (cur->data.status == VIEWING_COMPLETED ||
            cur->data.status == VIEWING_CANCELLED ||
            cur->data.status == VIEWING_MISSED) {
            continue;
        }
        return 1;
    }
    return 0;
}

static int tenant_has_deletable_viewing(int tenantId) {
    ViewingNode *cur;
    for (cur = g_db.viewings; cur; cur = cur->next) {
        if (cur->data.tenantId != tenantId) continue;
        if (cur->data.status == VIEWING_COMPLETED) continue;
        return 1;
    }
    return 0;
}

static void update_viewing_for_tenant(int tenantId) {
    int id;
    ViewingNode *v;
    char dt[20];
    int dur;
    int aid;
    if (!reload_database_for_sync()) return;

    if (!tenant_has_modifiable_viewing(tenantId)) {
        printf("无可修改的看房预约记录。\n");
        return;
    }

    id = input_int("看房ID: ", 1, 99999999);
    v = find_viewing(id);
    if (!v || v->data.tenantId != tenantId) {
        printf("记录不存在或无权限。\n");
        return;
    }
    if (v->data.status == VIEWING_COMPLETED || v->data.status == VIEWING_CANCELLED || v->data.status == VIEWING_MISSED) {
        printf("当前状态不可修改预约。\n");
        return;
    }

    printf("当前时间:%s 时长:%d 中介:%d\n", v->data.datetime, v->data.durationMinutes, v->data.agentId);
    printf("新时间(回车保持): ");
    read_line(dt, sizeof(dt));
    dur = v->data.durationMinutes;
    aid = v->data.agentId;

    if (input_yes_no("修改时长?")) dur = input_int("时长(分钟): ", 10, 600);
    if (input_yes_no("修改中介?")) {
        int related[256];
        int n;
        aid = input_int("中介ID(0表示待分配): ", 0, 4999);
        if (aid != 0 && !find_agent(aid)) {
            printf("中介不存在。\n");
            return;
        }
        n = collect_related_agents_for_house(v->data.houseId, related, 256);
        if (aid != 0 && n > 0 && !contains_agent_id(related, n, aid)) {
            printf("该中介不在此房源的对应中介列表中。\n");
            return;
        }
    }

    if (dt[0] && !validate_datetime(dt)) {
        printf("时间格式错误。\n");
        return;
    }
    if (dt[0] && !appointment_time_is_in_future(dt)) {
        printf("预约时间必须晚于当前时间。\n");
        return;
    }
    if (viewing_conflict(dt[0] ? dt : v->data.datetime, dur, v->data.houseId, aid, v->data.id)) {
        printf("预约冲突。\n");
        return;
    }

    if (dt[0]) {
        strncpy(v->data.datetime, dt, sizeof(v->data.datetime) - 1);
        v->data.datetime[sizeof(v->data.datetime) - 1] = '\0';
    }
    v->data.durationMinutes = dur;
    v->data.agentId = aid;
    printf("修改成功。\n");
}

static void list_my_rentals(int tenantId) {
    RentalNode *r;
    ui_section("我的租约");
    for (r = g_db.rentals; r; r = r->next) if (r->data.tenantId == tenantId) print_rental_detailed(&r->data);
}

static void process_pending_rentals_for_tenant(int tenantId) {
    while (1) {
        RentalNode *r;
        int found = 0;
        int targetId;
        int op;
        if (!reload_database_for_sync()) return;

        ui_section("待签合同");
        for (r = g_db.rentals; r; r = r->next) {
            if (r->data.tenantId != tenantId || r->data.signStatus != RENTAL_SIGN_PENDING) continue;
            print_rental_detailed(&r->data);
            found = 1;
        }
        if (!found) {
            printf("暂无待签合同。\n");
            return;
        }

        targetId = input_int("输入租约ID处理(0返回): ", 0, 99999999);
        if (targetId == 0) return;
        r = find_rental(targetId);
        if (!r || r->data.tenantId != tenantId || r->data.signStatus != RENTAL_SIGN_PENDING) {
            printf("租约不存在或已被处理。\n");
            continue;
        }

        op = input_int("1确认签订 2拒绝签订 3取消: ", 1, 3);
        if (op == 1) {
            HouseNode *h;
            if (has_open_contract_for_house(r->data.houseId, r->data.id)) {
                printf("房源存在冲突租约，当前合同无法签订，请联系中介。\n");
                continue;
            }
            r->data.signStatus = RENTAL_SIGN_CONFIRMED;
            r->data.status = RENTAL_ACTIVE;
            h = find_house(r->data.houseId);
            if (h && h->data.status != HOUSE_OFFLINE && h->data.status != HOUSE_PENDING) {
                h->data.status = HOUSE_RENTED;
            }
            autosave_default();
            printf("已确认签订。\n");
        } else if (op == 2) {
            r->data.signStatus = RENTAL_SIGN_REJECTED;
            r->data.status = RENTAL_EARLY;
            refresh_house_status(r->data.houseId);
            autosave_default();
            printf("已拒绝该合同。\n");
        }
    }
}

static void query_tenant_rentals(int tenantId) {
    printf("查询属性: 1.租约ID  2.房源ID  3.履约状态  4.合同日期范围  5.全部  6.关键字模糊  7.签约状态\n");
    int mode = input_int("请选择查询属性编号: ", 1, 7);
    RentalNode *r;
    int cnt = 0;
    if (mode == 1) {
        int id = input_int("租约ID: ", 1, 99999999);
        r = find_rental(id);
        if (!r || r->data.tenantId != tenantId) {
            printf("未找到。\n");
            return;
        }
        print_rental_detailed(&r->data);
        return;
    }
    if (mode == 2) {
        int houseId = input_int("房源ID: ", 1, 99999999);
        for (r = g_db.rentals; r; r = r->next) {
            if (r->data.tenantId == tenantId && r->data.houseId == houseId) {
                print_rental_detailed(&r->data);
                cnt++;
            }
        }
    } else if (mode == 3) {
        printf("履约状态可选: 0.有效  1.已到期  2.提前退租\n");
        int st = input_int("请选择履约状态编号: ", 0, 2);
        for (r = g_db.rentals; r; r = r->next) {
            if (r->data.tenantId == tenantId && r->data.status == st) {
                print_rental_detailed(&r->data);
                cnt++;
            }
        }
    } else if (mode == 4) {
        char start[11], end[11];
        while (1) {
            input_non_empty("签约开始日期(YYYY-MM-DD): ", start, sizeof(start));
            if (validate_date(start)) break;
            printf("日期格式错误。\n");
        }
        while (1) {
            input_non_empty("签约结束日期(YYYY-MM-DD): ", end, sizeof(end));
            if (validate_date(end)) break;
            printf("日期格式错误。\n");
        }
        if (compare_date_str(end, start) < 0) {
            printf("结束日期不能早于开始日期。\n");
            return;
        }
        for (r = g_db.rentals; r; r = r->next) {
            if (r->data.tenantId != tenantId) continue;
            if (compare_date_str(r->data.contractDate, start) < 0) continue;
            if (compare_date_str(r->data.contractDate, end) > 0) continue;
            print_rental_detailed(&r->data);
            cnt++;
        }
    } else if (mode == 5) {
        list_my_rentals(tenantId);
        return;
    } else if (mode == 6) {
        char kw[MAX_STR];
        input_non_empty("关键字(日期/状态/ID): ", kw, sizeof(kw));
        for (r = g_db.rentals; r; r = r->next) {
            char idBuf[32], houseBuf[32], tenantBuf[32];
            if (r->data.tenantId != tenantId) continue;
            snprintf(idBuf, sizeof(idBuf), "%d", r->data.id);
            snprintf(houseBuf, sizeof(houseBuf), "%d", r->data.houseId);
            snprintf(tenantBuf, sizeof(tenantBuf), "%d", r->data.tenantId);
            if (!contains_case_insensitive(r->data.contractDate, kw) &&
                !contains_case_insensitive(r->data.startDate, kw) &&
                !contains_case_insensitive(r->data.endDate, kw) &&
                !contains_case_insensitive(rental_sign_state_text(r->data.signStatus), kw) &&
                !contains_case_insensitive(rental_state_text(r->data.status), kw) &&
                !contains_case_insensitive(idBuf, kw) &&
                !contains_case_insensitive(houseBuf, kw) &&
                !contains_case_insensitive(tenantBuf, kw)) {
                continue;
            }
            print_rental_detailed(&r->data);
            cnt++;
        }
    } else {
        printf("签约状态可选: 0.待签  1.已签  2.拒签  3.撤销\n");
        int signSt = input_int("请选择签约状态编号: ", 0, 3);
        for (r = g_db.rentals; r; r = r->next) {
            if (r->data.tenantId == tenantId && r->data.signStatus == signSt) {
                print_rental_detailed(&r->data);
                cnt++;
            }
        }
    }
    if (!cnt) printf("未找到。\n");
}

static void sort_tenant_viewings(int tenantId) {
    printf("排序属性: 1.时间  2.时长  3.状态(同状态再按时间)\n");
    int mode = input_int("请选择排序属性编号: ", 1, 3);
    int ord = input_int("1升序 2降序: ", 1, 2);
    Viewing *arr;
    int n = 0;
    int i = 0, j;
    ViewingNode *v;

    for (v = g_db.viewings; v; v = v->next) if (v->data.tenantId == tenantId) n++;
    if (n == 0) {
        printf("暂无看房记录。\n");
        return;
    }

    arr = (Viewing *)malloc((size_t)n * sizeof(Viewing));
    if (!arr) {
        printf("内存不足。\n");
        return;
    }

    for (v = g_db.viewings; v; v = v->next) {
        if (v->data.tenantId == tenantId) arr[i++] = v->data;
    }

    for (i = 0; i < n - 1; ++i) {
        for (j = 0; j < n - 1 - i; ++j) {
            int swap = 0;
            if (mode == 1) {
                int c = strcmp(arr[j].datetime, arr[j + 1].datetime);
                swap = (ord == 1) ? (c > 0) : (c < 0);
            } else if (mode == 2) {
                swap = (ord == 1) ? (arr[j].durationMinutes > arr[j + 1].durationMinutes)
                                  : (arr[j].durationMinutes < arr[j + 1].durationMinutes);
            } else {
                int c = arr[j].status - arr[j + 1].status;
                if (c == 0) c = strcmp(arr[j].datetime, arr[j + 1].datetime);
                swap = (ord == 1) ? (c > 0) : (c < 0);
            }
            if (swap) {
                Viewing tmp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = tmp;
            }
        }
    }

    ui_section("我的看房排序结果");
    for (i = 0; i < n; ++i) print_viewing_detailed(&arr[i]);
    free(arr);
}

static void sort_tenant_rentals(int tenantId) {
    printf("排序属性: 1.合同日期  2.月租  3.起租日期(同日起再按月租)\n");
    int mode = input_int("请选择排序属性编号: ", 1, 3);
    int ord = input_int("1升序 2降序: ", 1, 2);
    Rental *arr;
    int n = 0;
    int i = 0, j;
    RentalNode *r;

    for (r = g_db.rentals; r; r = r->next) if (r->data.tenantId == tenantId) n++;
    if (n == 0) {
        printf("暂无租约记录。\n");
        return;
    }

    arr = (Rental *)malloc((size_t)n * sizeof(Rental));
    if (!arr) {
        printf("内存不足。\n");
        return;
    }

    for (r = g_db.rentals; r; r = r->next) {
        if (r->data.tenantId == tenantId) arr[i++] = r->data;
    }

    for (i = 0; i < n - 1; ++i) {
        for (j = 0; j < n - 1 - i; ++j) {
            int swap = 0;
            if (mode == 1) {
                int c = compare_date_str(arr[j].contractDate, arr[j + 1].contractDate);
                swap = (ord == 1) ? (c > 0) : (c < 0);
            } else if (mode == 2) {
                swap = (ord == 1) ? (arr[j].monthlyRent > arr[j + 1].monthlyRent)
                                  : (arr[j].monthlyRent < arr[j + 1].monthlyRent);
            } else {
                int c = compare_date_str(arr[j].startDate, arr[j + 1].startDate);
                if (c == 0) {
                    if (arr[j].monthlyRent < arr[j + 1].monthlyRent) c = -1;
                    else if (arr[j].monthlyRent > arr[j + 1].monthlyRent) c = 1;
                }
                swap = (ord == 1) ? (c > 0) : (c < 0);
            }
            if (swap) {
                Rental tmp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = tmp;
            }
        }
    }

    ui_section("我的租约排序结果");
    for (i = 0; i < n; ++i) print_rental_detailed(&arr[i]);
    free(arr);
}

static void list_my_viewings(int tenantId) {
    if (!find_tenant(tenantId)) {
        printf("租客不存在。\n");
        return;
    }
    view_my_appointments(tenantId);
}

static void query_tenant_viewings(int tenantId) {
    printf("查询属性: 1.看房ID  2.房源ID  3.状态  4.时间范围  5.全部(含中介回复摘要)  6.关键字模糊\n");
    int mode = input_int("请选择查询属性编号: ", 1, 6);
    ViewingNode *v;
    int cnt = 0;
    if (mode == 1) {
        int id = input_int("看房ID: ", 1, 99999999);
        v = find_viewing(id);
        if (!v || v->data.tenantId != tenantId) {
            printf("未找到。\n");
            return;
        }
        print_viewing_detailed(&v->data);
        return;
    }
    if (mode == 2) {
        int houseId = input_int("房源ID: ", 1, 99999999);
        for (v = g_db.viewings; v; v = v->next) {
            if (v->data.tenantId == tenantId && v->data.houseId == houseId) {
                print_viewing_detailed(&v->data);
                cnt++;
            }
        }
    } else if (mode == 3) {
        printf("看房状态可选: 0.待确认  1.已确认  2.已完成  3.已取消  4.未赴约\n");
        int st = input_int("请选择状态编号: ", 0, 4);
        for (v = g_db.viewings; v; v = v->next) {
            if (v->data.tenantId == tenantId && v->data.status == st) {
                print_viewing_detailed(&v->data);
                cnt++;
            }
        }
    } else if (mode == 4) {
        char start[20], end[20];
        while (1) {
            input_non_empty("开始时间(YYYY-MM-DD HH:MM): ", start, sizeof(start));
            if (validate_datetime(start)) break;
            printf("时间格式错误。\n");
        }
        while (1) {
            input_non_empty("结束时间(YYYY-MM-DD HH:MM): ", end, sizeof(end));
            if (validate_datetime(end)) break;
            printf("时间格式错误。\n");
        }
        if (strcmp(end, start) < 0) {
            printf("结束时间不能早于开始时间。\n");
            return;
        }
        for (v = g_db.viewings; v; v = v->next) {
            if (v->data.tenantId != tenantId) continue;
            if (strcmp(v->data.datetime, start) < 0) continue;
            if (strcmp(v->data.datetime, end) > 0) continue;
            print_viewing_detailed(&v->data);
            cnt++;
        }
    } else if (mode == 5) {
        list_my_viewings(tenantId);
        return;
    } else {
        char kw[MAX_STR];
        input_non_empty("关键字(时间/反馈/状态/ID): ", kw, sizeof(kw));
        for (v = g_db.viewings; v; v = v->next) {
            char idBuf[32], houseBuf[32], tenantBuf[32], agentBuf[32];
            if (v->data.tenantId != tenantId) continue;
            snprintf(idBuf, sizeof(idBuf), "%d", v->data.id);
            snprintf(houseBuf, sizeof(houseBuf), "%d", v->data.houseId);
            snprintf(tenantBuf, sizeof(tenantBuf), "%d", v->data.tenantId);
            snprintf(agentBuf, sizeof(agentBuf), "%d", v->data.agentId);
            if (!contains_case_insensitive(v->data.datetime, kw) &&
                !contains_case_insensitive(v->data.tenantFeedback, kw) &&
                !contains_case_insensitive(v->data.agentFeedback, kw) &&
                !contains_case_insensitive(viewing_state_text(v->data.status), kw) &&
                !contains_case_insensitive(idBuf, kw) &&
                !contains_case_insensitive(houseBuf, kw) &&
                !contains_case_insensitive(tenantBuf, kw) &&
                !contains_case_insensitive(agentBuf, kw)) {
                continue;
            }
            print_viewing_detailed(&v->data);
            cnt++;
        }
    }
    if (!cnt) printf("未找到。\n");
}

static void tenant_query_menu(int tenantId) {
    printf("查询菜单: 1.多条件查询房源(可预约)  2.查看房源与对应中介  3.组合筛选房源  4.我的看房  5.我的租约  6.房源时段可预约性\n");
    int ch = input_int("请选择查询菜单编号: ", 1, 6);
    if (ch == 1) {
        search_houses_for_tenant(tenantId);
    }
    else if (ch == 2) tenant_view_houses_with_agents();
    else if (ch == 3) query_houses_combo();
    else if (ch == 4) query_tenant_viewings(tenantId);
    else if (ch == 5) query_tenant_rentals(tenantId);
    else query_house_availability_by_time();
}

static void tenant_sort_menu(int tenantId) {
    printf("排序菜单: 1.房源  2.我的看房  3.我的租约\n");
    int ch = input_int("请选择排序菜单编号: ", 1, 3);
    if (ch == 1) sort_houses();
    else if (ch == 2) sort_tenant_viewings(tenantId);
    else sort_tenant_rentals(tenantId);
}

static void tenant_statistics_menu(int tenantId) {
    int ch = input_int("1看房统计 2租约统计 3按时间范围统计看房 4按月份统计租约: ", 1, 4);
    if (ch == 1) {
        ViewingNode *v;
        int total = 0;
        int statusCount[5] = {0, 0, 0, 0, 0};
        int durationSum = 0;
        for (v = g_db.viewings; v; v = v->next) {
            if (v->data.tenantId != tenantId) continue;
            total++;
            durationSum += v->data.durationMinutes;
            if (v->data.status >= 0 && v->data.status <= 4) statusCount[v->data.status]++;
        }
        printf("租客ID:%d 看房总数:%d 平均时长:%.2f分钟\n", tenantId, total,
               total ? (double)durationSum / total : 0.0);
        printf("待确认:%d 已确认:%d 已完成:%d 已取消:%d 未赴约:%d\n",
               statusCount[0], statusCount[1], statusCount[2], statusCount[3], statusCount[4]);
    } else if (ch == 2) {
        RentalNode *r;
        int total = 0;
        int active = 0, expired = 0, early = 0;
        double rentSum = 0.0;
        double daysSum = 0.0;
        for (r = g_db.rentals; r; r = r->next) {
            time_t ts, te;
            double days;
            if (r->data.tenantId != tenantId) continue;
            total++;
            rentSum += r->data.monthlyRent;
            if (r->data.status == RENTAL_ACTIVE) active++;
            else if (r->data.status == RENTAL_EXPIRED) expired++;
            else if (r->data.status == RENTAL_EARLY) early++;
            ts = date_to_time(r->data.startDate);
            te = date_to_time(r->data.endDate);
            if (ts != (time_t)-1 && te != (time_t)-1 && te > ts) {
                days = difftime(te, ts) / 86400.0;
                daysSum += days;
            }
        }
        printf("租客ID:%d 租约总数:%d 有效:%d 到期:%d 提前退租:%d\n",
               tenantId, total, active, expired, early);
        printf("月租总额:%.2f 平均月租:%.2f 平均租期:%.2f天\n",
               rentSum, total ? rentSum / total : 0.0, total ? daysSum / total : 0.0);
    } else if (ch == 3) {
        char start[20], end[20];
        ViewingNode *v;
        int cnt = 0;
        while (1) {
            input_non_empty("开始时间(YYYY-MM-DD HH:MM): ", start, sizeof(start));
            if (validate_datetime(start)) break;
            printf("时间格式错误。\n");
        }
        while (1) {
            input_non_empty("结束时间(YYYY-MM-DD HH:MM): ", end, sizeof(end));
            if (validate_datetime(end)) break;
            printf("时间格式错误。\n");
        }
        if (strcmp(end, start) < 0) {
            printf("结束时间不能早于开始时间。\n");
            return;
        }
        for (v = g_db.viewings; v; v = v->next) {
            if (v->data.tenantId != tenantId) continue;
            if (strcmp(v->data.datetime, start) < 0) continue;
            if (strcmp(v->data.datetime, end) > 0) continue;
            cnt++;
        }
        printf("时间范围内看房次数:%d\n", cnt);
    } else {
        char month[8];
        RentalNode *r;
        int cnt = 0;
        double rentSum = 0.0;
        while (1) {
            input_non_empty("签约月份(YYYY-MM): ", month, sizeof(month));
            if (strlen(month) == 7 && month[4] == '-') break;
            printf("月份格式错误。\n");
        }
        for (r = g_db.rentals; r; r = r->next) {
            if (r->data.tenantId != tenantId) continue;
            if (strncmp(r->data.contractDate, month, 7) != 0) continue;
            cnt++;
            rentSum += r->data.monthlyRent;
        }
        printf("月份:%s 租约数:%d 平均月租:%.2f\n", month, cnt, cnt ? rentSum / cnt : 0.0);
    }
}

static void change_agent_password(AgentNode *a) {
    char oldPwd[32], newPwd[32];
    input_non_empty("旧密码: ", oldPwd, sizeof(oldPwd));
    if (strcmp(oldPwd, a->data.password) != 0) {
        printf("旧密码错误。\n");
        return;
    }
    while (1) {
        input_non_empty("新密码(至少6位): ", newPwd, sizeof(newPwd));
        if ((int)strlen(newPwd) >= MIN_PASSWORD_LEN) break;
        printf("密码过短。\n");
    }
    strncpy(a->data.password, newPwd, sizeof(a->data.password) - 1);
    a->data.password[sizeof(a->data.password) - 1] = '\0';
    printf("密码修改成功。\n");
}

static void sort_houses(void) {
    printf("排序属性: 1.面积  2.租金  3.区域(同区域再按租金)\n");
    int mode = input_int("请选择排序属性编号: ", 1, 3);
    int ord = input_int("1升序 2降序: ", 1, 2);
    House *arr;
    int n = g_db.houseCount;
    int i = 0, j;
    HouseNode *h;

    if (n == 0) {
        printf("暂无房源。\n");
        return;
    }

    arr = (House *)malloc((size_t)n * sizeof(House));
    if (!arr) {
        printf("内存不足。\n");
        return;
    }

    for (h = g_db.houses; h; h = h->next) arr[i++] = h->data;

    for (i = 0; i < n - 1; ++i) {
        for (j = 0; j < n - i - 1; ++j) {
            int swap;
            if (mode == 3) {
                int c = strcmp(arr[j].region, arr[j + 1].region);
                if (c == 0) {
                    double a = arr[j].price;
                    double b = arr[j + 1].price;
                    swap = (ord == 1) ? (a > b) : (a < b);
                } else {
                    swap = (ord == 1) ? (c > 0) : (c < 0);
                }
            } else {
                double a = (mode == 1) ? arr[j].area : arr[j].price;
                double b = (mode == 1) ? arr[j + 1].area : arr[j + 1].price;
                swap = (ord == 1) ? (a > b) : (a < b);
            }
            if (swap) {
                House t = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = t;
            }
        }
    }

    ui_section("房源排序结果");
    for (i = 0; i < n; ++i) print_house_detailed(&arr[i]);
    free(arr);
}

static void query_houses_combo(void) {
    char city[MAX_STR], region[MAX_STR], keyword[MAX_STR];
    CategoryList cityChoices;
    double minP, maxP, minA, maxA;
    int state;
    int cnt = 0;
    HouseNode *h;

    collect_city_choices(&cityChoices);
    input_optional_filter_with_choices(&cityChoices, "城市", city, sizeof(city));
    input_optional_filter_with_choices(&g_db.regions, "区域", region, sizeof(region));
    printf("地址/小区关键字(可空): ");
    read_line(keyword, sizeof(keyword));
    minP = input_double("最低租金: ", 0.0, 10000000.0);
    maxP = input_double("最高租金: ", minP, 10000000.0);
    minA = input_double("最小面积: ", 0.0, 100000.0);
    maxA = input_double("最大面积: ", minA, 100000.0);
    printf("状态可选: -1.全部  0.空闲  1.已出租  2.待审核  3.下架\n");
    state = input_int("请选择状态编号: ", -1, 3);

    for (h = g_db.houses; h; h = h->next) {
        if (!contains_case_insensitive(h->data.city, city)) continue;
        if (region[0] && strcmp(h->data.region, region) != 0) continue;
        if (!contains_case_insensitive(h->data.community, keyword) && !contains_case_insensitive(h->data.address, keyword)) continue;
        if (h->data.price < minP || h->data.price > maxP) continue;
        if (h->data.area < minA || h->data.area > maxA) continue;
        if (state != -1 && h->data.status != state) continue;
        print_house_detailed(&h->data);
        cnt++;
    }
    printf("查询完成，共%d条。\n", cnt);
}

static void query_house_availability_by_time(void) {
    int houseId;
    char dt[20];
    int dur;
    HouseNode *h;
    houseId = input_int("房源ID: ", 1, 99999999);
    h = find_house(houseId);
    if (!h) {
        printf("房源不存在。\n");
        return;
    }
    while (1) {
        input_non_empty("查询时间(YYYY-MM-DD HH:MM): ", dt, sizeof(dt));
        if (!validate_datetime(dt)) {
            printf("时间格式错误。\n");
            continue;
        }
        if (!appointment_time_is_in_future(dt)) {
            printf("查询时间必须晚于当前时间。\n");
            continue;
        }
        break;
    }
    dur = input_int("时长(分钟): ", 10, 600);
    if (!house_is_open_for_viewing(&h->data)) {
        printf("该房源当前不可预约。\n");
        return;
    }
    if (viewing_conflict(dt, dur, houseId, 0, -1)) {
        printf("该时段房源已被预约冲突占用。\n");
    } else {
        printf("该时段房源可预约。\n");
    }
}

static int category_value_in_use(int kind, const char *value) {
    HouseNode *h;
    for (h = g_db.houses; h; h = h->next) {
        if (kind == 1 && strcmp(h->data.region, value) == 0) return 1;
        if (kind == 2 && strcmp(h->data.floorNote, value) == 0) return 1;
        if (kind == 3 && strcmp(h->data.orientation, value) == 0) return 1;
        if (kind == 4 && strcmp(h->data.houseType, value) == 0) return 1;
        if (kind == 5 && strcmp(h->data.decoration, value) == 0) return 1;
    }
    return 0;
}

static void category_manage_one(CategoryList *list, const char *title, int kind) {
    int ch;
    while (1) {
        char buf[MAX_STR];
        int i;
        printf("\n--- 分类管理: %s ---\n", title);
        category_print(list, title);
        printf("1. 新增分类项\n");
        printf("2. 删除分类项\n");
        printf("0. 返回\n");
        ch = input_int("请选择操作编号: ", 0, 2);
        if (ch == 0) return;
        if (ch == 1) {
            input_non_empty("输入分类值: ", buf, sizeof(buf));
            for (i = 0; i < list->count; ++i) {
                if (strcmp(list->items[i], buf) == 0) {
                    printf("分类项已存在。\n");
                    break;
                }
            }
            if (i < list->count) continue;
            if (list->count >= MAX_CATEGORY_ITEMS) {
                printf("分类数量已达上限。\n");
                continue;
            }
            strncpy(list->items[list->count], buf, MAX_STR - 1);
            list->items[list->count][MAX_STR - 1] = '\0';
            list->count++;
            printf("新增成功。\n");
        } else {
            int idx;
            if (list->count == 0) {
                printf("暂无可删除项。\n");
                continue;
            }
            idx = input_int("输入要删除的编号: ", 1, list->count) - 1;
            if (category_value_in_use(kind, list->items[idx])) {
                printf("该分类已被房源使用，不能删除。\n");
                continue;
            }
            for (i = idx; i < list->count - 1; ++i) {
                strcpy(list->items[i], list->items[i + 1]);
            }
            list->count--;
            printf("删除成功。\n");
        }
    }
}

static void admin_category_menu(void) {
    int ch;
    while (1) {
        printf("\n====== 分类信息管理 ======\n");
        printf("1. 区域\n");
        printf("2. 楼层说明\n");
        printf("3. 朝向\n");
        printf("4. 房型\n");
        printf("5. 装修\n");
        printf("0. 返回\n");
        ch = input_int("请选择分类编号: ", 0, 5);
        if (ch == 0) return;
        if (ch == 1) category_manage_one(&g_db.regions, "区域", 1);
        else if (ch == 2) category_manage_one(&g_db.floorNotes, "楼层说明", 2);
        else if (ch == 3) category_manage_one(&g_db.orientations, "朝向", 3);
        else if (ch == 4) category_manage_one(&g_db.houseTypes, "房型", 4);
        else category_manage_one(&g_db.decorations, "装修", 5);
    }
}

static void sort_viewings_all(void) {
    printf("排序属性: 1.时间  2.时长  3.状态(同状态再按时间)\n");
    int mode = input_int("请选择排序属性编号: ", 1, 3);
    int ord = input_int("1升序 2降序: ", 1, 2);
    Viewing *arr;
    int n = g_db.viewingCount;
    int i = 0, j;
    ViewingNode *v;

    if (n == 0) {
        printf("暂无看房记录。\n");
        return;
    }
    arr = (Viewing *)malloc((size_t)n * sizeof(Viewing));
    if (!arr) {
        printf("内存不足。\n");
        return;
    }
    for (v = g_db.viewings; v; v = v->next) arr[i++] = v->data;

    for (i = 0; i < n - 1; ++i) {
        for (j = 0; j < n - 1 - i; ++j) {
            int swap = 0;
            if (mode == 1) {
                int c = strcmp(arr[j].datetime, arr[j + 1].datetime);
                swap = (ord == 1) ? (c > 0) : (c < 0);
            } else if (mode == 2) {
                swap = (ord == 1) ? (arr[j].durationMinutes > arr[j + 1].durationMinutes)
                                  : (arr[j].durationMinutes < arr[j + 1].durationMinutes);
            } else {
                int c = arr[j].status - arr[j + 1].status;
                if (c == 0) c = strcmp(arr[j].datetime, arr[j + 1].datetime);
                swap = (ord == 1) ? (c > 0) : (c < 0);
            }
            if (swap) {
                Viewing tmp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = tmp;
            }
        }
    }

    ui_section("看房排序结果");
    for (i = 0; i < n; ++i) print_viewing_detailed(&arr[i]);
    free(arr);
}

static void sort_rentals_all(void) {
    printf("排序属性: 1.合同日期  2.月租  3.起租日期(同日起再按月租)\n");
    int mode = input_int("请选择排序属性编号: ", 1, 3);
    int ord = input_int("1升序 2降序: ", 1, 2);
    Rental *arr;
    int n = g_db.rentalCount;
    int i = 0, j;
    RentalNode *r;

    if (n == 0) {
        printf("暂无租约记录。\n");
        return;
    }
    arr = (Rental *)malloc((size_t)n * sizeof(Rental));
    if (!arr) {
        printf("内存不足。\n");
        return;
    }
    for (r = g_db.rentals; r; r = r->next) arr[i++] = r->data;

    for (i = 0; i < n - 1; ++i) {
        for (j = 0; j < n - 1 - i; ++j) {
            int swap = 0;
            if (mode == 1) {
                int c = compare_date_str(arr[j].contractDate, arr[j + 1].contractDate);
                swap = (ord == 1) ? (c > 0) : (c < 0);
            } else if (mode == 2) {
                swap = (ord == 1) ? (arr[j].monthlyRent > arr[j + 1].monthlyRent)
                                  : (arr[j].monthlyRent < arr[j + 1].monthlyRent);
            } else {
                int c = compare_date_str(arr[j].startDate, arr[j + 1].startDate);
                if (c == 0) {
                    if (arr[j].monthlyRent < arr[j + 1].monthlyRent) c = -1;
                    else if (arr[j].monthlyRent > arr[j + 1].monthlyRent) c = 1;
                }
                swap = (ord == 1) ? (c > 0) : (c < 0);
            }
            if (swap) {
                Rental tmp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = tmp;
            }
        }
    }

    ui_section("租约排序结果");
    for (i = 0; i < n; ++i) print_rental_detailed(&arr[i]);
    free(arr);
}

static void admin_sort_menu(void) {
    printf("排序菜单: 1.房源  2.看房  3.租约\n");
    int ch = input_int("请选择排序菜单编号: ", 1, 3);
    if (ch == 1) sort_houses();
    else if (ch == 2) sort_viewings_all();
    else sort_rentals_all();
}

static void admin_statistics_menu(void) {
    int ch = input_int("1房源统计 2看房统计 3租约统计 4租客租房统计 5按月统计签约: ", 1, 5);
    if (ch == 1) {
        HouseNode *h;
        int total = 0, vacant = 0, rented = 0, pending = 0, offline = 0;
        double areaSum = 0.0, priceSum = 0.0;
        for (h = g_db.houses; h; h = h->next) {
            total++;
            areaSum += h->data.area;
            priceSum += h->data.price;
            if (h->data.status == HOUSE_VACANT) vacant++;
            else if (h->data.status == HOUSE_RENTED) rented++;
            else if (h->data.status == HOUSE_PENDING) pending++;
            else if (h->data.status == HOUSE_OFFLINE) offline++;
        }
        printf("房源总数:%d 空闲:%d 已出租:%d 待审核:%d 下架:%d\n", total, vacant, rented, pending, offline);
        printf("平均面积:%.2f 平均挂牌租金:%.2f 出租率:%.2f%%\n",
               total ? areaSum / total : 0.0,
               total ? priceSum / total : 0.0,
               total ? (double)rented * 100.0 / total : 0.0);
    } else if (ch == 2) {
        ViewingNode *v;
        int total = 0;
        int statusCount[5] = {0, 0, 0, 0, 0};
        int durationSum = 0;
        for (v = g_db.viewings; v; v = v->next) {
            total++;
            durationSum += v->data.durationMinutes;
            if (v->data.status >= 0 && v->data.status <= 4) statusCount[v->data.status]++;
        }
        printf("看房总数:%d 平均时长:%.2f分钟\n", total, total ? (double)durationSum / total : 0.0);
        printf("待确认:%d 已确认:%d 已完成:%d 已取消:%d 未赴约:%d\n",
               statusCount[0], statusCount[1], statusCount[2], statusCount[3], statusCount[4]);
    } else if (ch == 3) {
        RentalNode *r;
        int total = 0, active = 0, expired = 0, early = 0;
        double rentSum = 0.0, daysSum = 0.0;
        for (r = g_db.rentals; r; r = r->next) {
            time_t ts = date_to_time(r->data.startDate);
            time_t te = date_to_time(r->data.endDate);
            total++;
            rentSum += r->data.monthlyRent;
            if (r->data.status == RENTAL_ACTIVE) active++;
            else if (r->data.status == RENTAL_EXPIRED) expired++;
            else if (r->data.status == RENTAL_EARLY) early++;
            if (ts != (time_t)-1 && te != (time_t)-1 && te > ts) {
                daysSum += difftime(te, ts) / 86400.0;
            }
        }
        printf("租约总数:%d 有效:%d 到期:%d 提前退租:%d\n", total, active, expired, early);
        printf("平均月租:%.2f 平均租期:%.2f天\n",
               total ? rentSum / total : 0.0,
               total ? daysSum / total : 0.0);
    } else if (ch == 4) {
        int tenantId = input_int("租客ID: ", TENANT_ID_MIN, TENANT_ID_MAX);
        RentalNode *r;
        int cnt = 0, active = 0;
        double rentSum = 0.0;
        for (r = g_db.rentals; r; r = r->next) {
            if (r->data.tenantId != tenantId) continue;
            cnt++;
            rentSum += r->data.monthlyRent;
            if (r->data.status == RENTAL_ACTIVE) active++;
        }
        printf("租客ID:%d 租约数:%d 当前有效:%d 累计月租:%.2f\n", tenantId, cnt, active, rentSum);
    } else {
        char month[8];
        RentalNode *r;
        int cnt = 0;
        while (1) {
            input_non_empty("签约月份(YYYY-MM): ", month, sizeof(month));
            if (strlen(month) == 7 && month[4] == '-') break;
            printf("月份格式错误。\n");
        }
        for (r = g_db.rentals; r; r = r->next) {
            if (strncmp(r->data.contractDate, month, 7) == 0) cnt++;
        }
        printf("月份:%s 签约数量:%d\n", month, cnt);
    }
}

static void change_admin_password(void) {
    char oldPwd[32], newPwd[32];
    input_non_empty("旧密码: ", oldPwd, sizeof(oldPwd));
    if (strcmp(oldPwd, g_db.adminPassword) != 0) {
        printf("旧密码错误。\n");
        return;
    }
    while (1) {
        input_non_empty("新密码(至少6位): ", newPwd, sizeof(newPwd));
        if ((int)strlen(newPwd) >= MIN_PASSWORD_LEN) break;
        printf("密码过短。\n");
    }
    strncpy(g_db.adminPassword, newPwd, sizeof(g_db.adminPassword) - 1);
    g_db.adminPassword[sizeof(g_db.adminPassword) - 1] = '\0';
    printf("管理员密码修改成功。\n");
}

static void backup_data_to_custom_file(void) {
    char file[256];
    input_non_empty("备份文件路径: ", file, sizeof(file));
    if (save_to_file(file)) printf("备份完成。\n");
}

static void restore_data_from_custom_file(void) {
    char file[256];
    input_non_empty("恢复文件路径: ", file, sizeof(file));
    if (!load_from_file(file)) {
        printf("恢复失败。\n");
        return;
    }
    update_expired_rentals();
    printf("恢复成功。\n");
}

static void admin_menu(void) {
    BackContext ctx;
    BackContext *prev = g_back_ctx;
    g_back_ctx = &ctx;
    ctx.active = 1;
    if (setjmp(ctx.env) != 0) {
        ui_warn("已返回上一级（管理员菜单）。");
    }
    int ch;
    while (1) {
        int needSave = 0;
        if (!reload_database_for_sync()) {
            printf("数据同步失败，已返回上一级。\n");
            g_back_ctx = prev;
            return;
        }
        printf("\n====== 管理员菜单 ======\n");
        printf("提示: 任意输入处可用 # 回退上一级；-1 在当前不作为有效值时也可回退。\n");
        printf("1. 信息管理-中介新增\n");
        printf("2. 信息管理-中介修改\n");
        printf("3. 信息管理-中介删除\n");
        printf("4. 信息查询-中介\n");
        printf("5. 信息查询-租客\n");
        printf("6. 信息管理-租客修改\n");
        printf("7. 信息管理-租客删除\n");
        printf("8. 系统维护-重置租客密码\n");
        printf("9. 信息管理-新增房源(免审上架)\n");
        printf("10. 信息管理-修改房源\n");
        printf("11. 信息管理-删除房源\n");
        printf("12. 看房管理-审核中介提交房源\n");
        printf("13. 信息查询-房源列表\n");
        printf("14. 信息查询-组合查房\n");
        printf("15. 信息排序\n");
        printf("16. 看房管理-分配中介\n");
        printf("17. 信息查询-看房记录\n");
        printf("18. 信息查询-租约记录\n");
        printf("19. 租房管理-租约状态管理\n");
        printf("20. 信息查询-按签约日期查租约\n");
        printf("21. 信息查询-房源时段可预约性\n");
        printf("22. 系统维护-重置中介密码\n");
        printf("23. 系统维护-保存当前数据\n");
        printf("24. 其他-生成演示数据\n");
        printf("25. 信息管理-分类信息管理\n");
        printf("26. 信息统计\n");
        printf("27. 系统维护-修改管理员密码\n");
        printf("28. 系统维护-备份到指定文件\n");
        printf("29. 系统维护-从指定文件恢复\n");
        printf("0. 退出登录\n");
        ch = input_int("请选择菜单编号: ", 0, 29);
        if (ch == 0) {
            g_back_ctx = prev;
            return;
        }
        if (ch == 1) {
            add_agent_item();
            needSave = 1;
        }
        else if (ch == 2) {
            update_agent_item();
            needSave = 1;
        }
        else if (ch == 3) {
            delete_agent_item();
            needSave = 1;
        }
        else if (ch == 4) query_agent_items();
        else if (ch == 5) query_tenant_items();
        else if (ch == 6) {
            update_tenant_item();
            needSave = 1;
        }
        else if (ch == 7) {
            delete_tenant_item();
            needSave = 1;
        }
        else if (ch == 8) {
            reset_tenant_password_by_admin();
            needSave = 1;
        }
        else if (ch == 9) {
            add_house_item();
            needSave = 1;
        }
        else if (ch == 10) {
            update_house_item();
            needSave = 1;
        }
        else if (ch == 11) {
            delete_house_item();
            needSave = 1;
        }
        else if (ch == 12) {
            audit_pending_houses();
        }
        else if (ch == 13) list_houses();
        else if (ch == 14) query_houses_combo();
        else if (ch == 15) admin_sort_menu();
        else if (ch == 16) {
            assign_viewings_admin();
            needSave = 1;
        }
        else if (ch == 17) list_viewings_all();
        else if (ch == 18) list_rentals_admin_filtered();
        else if (ch == 19) {
            update_rental_status_admin();
            needSave = 1;
        }
        else if (ch == 20) query_rentals_by_contract_range();
        else if (ch == 21) query_house_availability_by_time();
        else if (ch == 22) {
            reset_agent_password_by_admin();
            needSave = 1;
        }
        else if (ch == 23) save_to_file(g_data_file);
        else if (ch == 24) {
            seed_demo_data();
            needSave = 1;
            ui_success("演示数据已生成/补齐。");
        } else if (ch == 25) {
            admin_category_menu();
            needSave = 1;
        }
        else if (ch == 26) admin_statistics_menu();
        else if (ch == 27) {
            change_admin_password();
            needSave = 1;
        }
        else if (ch == 28) backup_data_to_custom_file();
        else {
            restore_data_from_custom_file();
            needSave = 1;
        }
        if (needSave) autosave_default();
    }
}

static void agent_menu(AgentNode *a) {
    BackContext ctx;
    BackContext *prev = g_back_ctx;
    g_back_ctx = &ctx;
    ctx.active = 1;
    if (setjmp(ctx.env) != 0) {
        ui_warn("已返回上一级（中介菜单）。");
    }
    int agentId = a->data.id;
    int ch;
    while (1) {
        int needSave = 0;
        if (!reload_database_for_sync()) {
            printf("数据同步失败，已返回上一级。\n");
            g_back_ctx = prev;
            return;
        }
        printf("\n====== 中介菜单 ======\n");
        printf("提示: 任意输入处可用 # 回退上一级；-1 在当前不作为有效值时也可回退。\n");
        printf("1. 新增租房合同\n");
        printf("2. 修改合同状态\n");
        printf("3. 删除租房合同\n");
        printf("4. 查看我经手租约\n");
        printf("5. 业务查询(租客预约/租约)\n");
        printf("6. 业务排序(预约/租约/房源)\n");
        printf("7. 查看我负责看房\n");
        printf("8. 修改看房记录\n");
        printf("9. 删除看房记录\n");
        printf("10. 查询房源时段可预约性\n");
        printf("11. 信息统计\n");
        printf("12. 修改密码\n");
        printf("13. 新增房源(提交待审核)\n");
        printf("14. 处理待处理预约(同意/拒绝)\n");
        printf("15. 我的提交房源(状态/重提)\n");
        printf("0. 退出登录\n");
        ch = input_int("请选择菜单编号: ", 0, 15);
        if (ch == 0) {
            g_back_ctx = prev;
            return;
        }
        if (ch == 1) {
            add_rental_for_agent(agentId);
            needSave = 1;
        }
        else if (ch == 2) {
            int id = input_int("租房ID: ", 1, 99999999);
            RentalNode *r = find_rental(id);
            if (!r || r->data.agentId != agentId) {
                printf("记录不存在或无权限。\n");
            } else if (r->data.signStatus != RENTAL_SIGN_CONFIRMED) {
                printf("合同未签订，不能修改履约状态。\n");
            } else {
                printf("履约状态可选: 0.有效  1.到期  2.提前退租\n");
                r->data.status = input_int("请选择履约状态编号: ", 0, 2);
                refresh_house_status(r->data.houseId);
                needSave = 1;
            }
        } else if (ch == 3) {
            int id = input_int("租房ID: ", 1, 99999999);
            RentalNode *r = find_rental(id);
            int houseId;
            if (!r || r->data.agentId != agentId) {
                printf("记录不存在或无权限。\n");
            } else {
                houseId = r->data.houseId;
                remove_rental(id);
                refresh_house_status(houseId);
                printf("删除成功。\n");
                needSave = 1;
            }
        } else if (ch == 4) {
            RentalNode *r;
            ui_section("我的租约");
            for (r = g_db.rentals; r; r = r->next) {
                if (r->data.agentId == agentId) print_rental_detailed(&r->data);
            }
        } else if (ch == 5) {
            agent_query_menu(agentId);
        } else if (ch == 6) {
            agent_sort_menu(agentId);
        } else if (ch == 7) {
            list_agent_viewings(agentId);
        } else if (ch == 8) {
            update_viewing_for_agent(agentId);
            needSave = 1;
        } else if (ch == 9) {
            delete_viewing_for_agent(agentId);
            needSave = 1;
        } else if (ch == 10) {
            query_house_availability_by_time();
        } else if (ch == 11) {
            agent_statistics_menu(agentId);
        } else if (ch == 12) {
            AgentNode *current = find_agent(agentId);
            if (!current) {
                printf("当前中介账号已不存在，请重新登录。\n");
            } else {
                change_agent_password(current);
                needSave = 1;
            }
        } else if (ch == 13) {
            add_house_item_for_agent(agentId);
        } else if (ch == 14) {
            process_pending_viewings_for_agent(agentId);
        } else {
            agent_submitted_houses_menu(agentId);
        }
        if (needSave) autosave_default();
    }
}

static void tenant_menu(TenantNode *t) {
    BackContext ctx;
    BackContext *prev = g_back_ctx;
    g_back_ctx = &ctx;
    ctx.active = 1;
    if (setjmp(ctx.env) != 0) {
        ui_warn("已返回上一级（租客菜单）。");
    }
    int tenantId = t->data.id;
    int ch;
    while (1) {
        int needSave = 0;
        if (!reload_database_for_sync()) {
            printf("数据同步失败，已返回上一级。\n");
            g_back_ctx = prev;
            return;
        }
        printf("\n====== 租客菜单 ======\n");
        printf("提示: 任意输入处可用 # 回退上一级；-1 在当前不作为有效值时也可回退。\n");
        printf("1. 查询房源并预约(推荐)\n");
        printf("2. 修改看房预约\n");
        printf("3. 删除看房预约\n");
        printf("4. 信息查询\n");
        printf("5. 信息排序\n");
        printf("6. 信息统计\n");
        printf("7. 我的看房预约\n");
        printf("8. 我的租房合同\n");
        printf("9. 填写看房反馈\n");
        printf("10. 修改个人信息\n");
        printf("11. 处理待签合同\n");
        printf("0. 退出登录\n");
        ch = input_int("请选择菜单编号: ", 0, 11);
        if (ch == 0) {
            g_back_ctx = prev;
            return;
        }
        if (!find_tenant(tenantId)) {
            printf("当前租客账号已不存在，请重新登录。\n");
            g_back_ctx = prev;
            return;
        }
        if (ch == 1) {
            add_viewing_for_tenant(tenantId);
        }
        else if (ch == 2) {
            if (!tenant_has_modifiable_viewing(tenantId)) {
                printf("无可修改的看房预约记录。\n");
                continue;
            }
            update_viewing_for_tenant(tenantId);
            needSave = 1;
        } else if (ch == 3) {
            if (!tenant_has_deletable_viewing(tenantId)) {
                printf("无可删除的看房预约记录。\n");
                continue;
            }
            int id = input_int("看房ID: ", 1, 99999999);
            ViewingNode *v = find_viewing(id);
            if (!v || v->data.tenantId != tenantId) {
                printf("记录不存在或无权限。\n");
            } else if (v->data.status == VIEWING_COMPLETED) {
                printf("已完成看房记录不允许删除。\n");
            } else {
                remove_viewing(id);
                printf("删除成功。\n");
                needSave = 1;
            }
        } else if (ch == 4) {
            tenant_query_menu(tenantId);
        } else if (ch == 5) {
            tenant_sort_menu(tenantId);
        } else if (ch == 6) {
            tenant_statistics_menu(tenantId);
        } else if (ch == 7) {
            list_my_viewings(tenantId);
        } else if (ch == 8) {
            list_my_rentals(tenantId);
        } else if (ch == 9) {
            int id = input_int("看房ID: ", 1, 99999999);
            ViewingNode *v = find_viewing(id);
            if (!v || v->data.tenantId != tenantId) {
                printf("记录不存在或无权限。\n");
            } else if (v->data.status != VIEWING_COMPLETED) {
                printf("仅已完成看房可反馈。\n");
            } else {
                input_non_empty("租客反馈: ", v->data.tenantFeedback, sizeof(v->data.tenantFeedback));
                needSave = 1;
            }
        } else if (ch == 10) {
            char buf[MAX_STR];
            char masked[32];
            int profileUpdated = 0;
            TenantNode *current = find_tenant(tenantId);
            if (!current) {
                printf("当前租客账号已不存在，请重新登录。\n");
                g_back_ctx = prev;
                return;
            }
            mask_id_card(current->data.idCard, masked, sizeof(masked));
            printf("当前姓名:%s 电话:%s 身份证:%s\n", current->data.name, current->data.phone, masked);
            printf("新姓名(回车保持): ");
            read_line(buf, sizeof(buf));
            if (buf[0]) {
                strncpy(current->data.name, buf, sizeof(current->data.name) - 1);
                current->data.name[sizeof(current->data.name) - 1] = '\0';
                profileUpdated = 1;
            }
            while (1) {
                printf("新电话(回车保持): ");
                read_line(buf, sizeof(buf));
                if (!buf[0]) break;
                if (!validate_phone(buf)) {
                    printf("电话格式错误。\n");
                    continue;
                }
                {
                    TenantNode *dup = find_tenant_by_phone(buf);
                    if (dup && dup->data.id != tenantId) {
                        printf("电话已存在，不能重复。\n");
                        continue;
                    }
                }
                strncpy(current->data.phone, buf, sizeof(current->data.phone) - 1);
                current->data.phone[sizeof(current->data.phone) - 1] = '\0';
                profileUpdated = 1;
                break;
            }
            while (1) {
                printf("新身份证18位(回车保持): ");
                read_line(buf, sizeof(buf));
                if (!buf[0]) break;
                if (!validate_id_card(buf)) {
                    printf("身份证格式错误。\n");
                    continue;
                }
                strncpy(current->data.idCard, buf, sizeof(current->data.idCard) - 1);
                current->data.idCard[sizeof(current->data.idCard) - 1] = '\0';
                profileUpdated = 1;
                break;
            }
            if (input_yes_no("是否修改密码?")) {
                char idCard[20], newPwd[32];
                input_non_empty("身份证(18位): ", idCard, sizeof(idCard));
                if (strcmp(idCard, current->data.idCard) != 0) {
                    printf("身份证校验失败。\n");
                } else {
                    while (1) {
                        input_non_empty("新密码(至少6位): ", newPwd, sizeof(newPwd));
                        if ((int)strlen(newPwd) >= MIN_PASSWORD_LEN) break;
                        printf("密码过短。\n");
                    }
                    strncpy(current->data.password, newPwd, sizeof(current->data.password) - 1);
                    current->data.password[sizeof(current->data.password) - 1] = '\0';
                    needSave = 1;
                }
            }
            if (profileUpdated) needSave = 1;
        } else {
            if (!tenant_has_pending_rental(tenantId)) {
                printf("暂无待签合同。\n");
                continue;
            }
            process_pending_rentals_for_tenant(tenantId);
        }
        if (needSave) autosave_default();
    }
}

static int login_password_check(const char *realPwd, int role, int accountId) {
    char pwd[32];
    if (login_is_locked(role, accountId)) return 0;
    while (1) {
        input_non_empty("密码: ", pwd, sizeof(pwd));
        if (strcmp(pwd, realPwd) == 0) {
            login_record_success(role, accountId);
            return 1;
        }
        login_record_fail(role, accountId);
        if (login_is_locked(role, accountId)) return 0;
        {
            LoginGuard *g = get_login_guard(role, accountId);
            int remain = g ? (MAX_LOGIN_ATTEMPTS - g->failed) : 0;
            printf("密码错误，剩余次数:%d\n", remain);
        }
    }
}

static void tenant_register(void) {
    Tenant t;
    if (!reload_database_for_sync()) {
        printf("数据同步失败，注册已取消。\n");
        return;
    }
    memset(&t, 0, sizeof(t));
    t.id = generate_next_tenant_id();
    if (t.id == 0) {
        printf("系统繁忙，暂时无法分配租客ID，请稍后重试。\n");
        return;
    }
    input_non_empty("姓名: ", t.name, sizeof(t.name));
    while (1) {
        input_non_empty("电话: ", t.phone, sizeof(t.phone));
        if (!validate_phone(t.phone)) {
            printf("电话格式错误。\n");
            continue;
        }
        if (find_tenant_by_phone(t.phone)) {
            printf("电话已存在，不能重复。\n");
            continue;
        }
        break;
    }
    while (1) {
        input_non_empty("身份证(18位): ", t.idCard, sizeof(t.idCard));
        if (validate_id_card(t.idCard)) break;
        printf("身份证格式错误。\n");
    }
    while (1) {
        input_non_empty("密码(至少6位): ", t.password, sizeof(t.password));
        if ((int)strlen(t.password) >= MIN_PASSWORD_LEN) break;
        printf("密码过短。\n");
    }
    if (!append_tenant(&t)) {
        printf("内存不足。\n");
        return;
    }
    autosave_default();
    printf("注册成功，系统分配租客ID: %d\n", t.id);
    {
        char pauseBuf[8];
        printf("请记录该ID，用于后续登录。按回车返回主菜单...");
        read_line(pauseBuf, sizeof(pauseBuf));
    }
}

static void login_admin(void) {
    if (!reload_database_for_sync()) {
        return;
    }
    if (!login_password_check(g_db.adminPassword, LOGIN_ROLE_ADMIN, 0)) {
        return;
    }
    admin_menu();
}

static void login_agent(void) {
    if (!reload_database_for_sync()) {
        return;
    }
    int id = input_int("中介ID: ", 1000, 4999);
    AgentNode *a = find_agent(id);
    if (!a) {
        printf("中介不存在。\n");
        return;
    }
    if (!login_password_check(a->data.password, LOGIN_ROLE_AGENT, a->data.id)) {
        return;
    }
    agent_menu(a);
}

static void login_tenant(void) {
    if (!reload_database_for_sync()) {
        return;
    }
    int mode = input_int("登录方式 1租客ID 2手机号: ", 1, 2);
    TenantNode *t = NULL;
    if (mode == 1) {
        int id = input_int("租客ID: ", TENANT_ID_MIN, TENANT_ID_MAX);
        t = find_tenant(id);
    } else {
        char phone[20];
        input_non_empty("手机号: ", phone, sizeof(phone));
        t = find_tenant_by_phone(phone);
    }
    if (!t) {
        printf("租客不存在。\n");
        return;
    }
    if (!login_password_check(t->data.password, LOGIN_ROLE_TENANT, t->data.id)) {
        return;
    }
    tenant_menu(t);
}

static void recover_admin_password(void) {
    printf("出于安全原因，管理员不支持未登录找回密码。请使用管理员登录后在系统维护中修改密码。\n");
}

static void recover_agent_password(void) {
    int id = input_int("中介ID: ", 1000, 4999);
    char phone[20];
    char idCard[20];
    char newPwd[32];
    if (!reload_database_for_sync()) {
        printf("数据同步失败，找回已取消。\n");
        return;
    }
    AgentNode *a = find_agent(id);
    if (!a) {
        printf("中介不存在。\n");
        return;
    }
    if (!a->data.idCard[0]) {
        printf("该中介账号尚未录入身份证信息，请联系管理员补录后再找回密码。\n");
        return;
    }
    input_non_empty("绑定手机号: ", phone, sizeof(phone));
    if (strcmp(phone, a->data.phone) != 0) {
        printf("手机号校验失败。\n");
        return;
    }
    input_non_empty("身份证(18位): ", idCard, sizeof(idCard));
    if (strcmp(idCard, a->data.idCard) != 0) {
        printf("身份证校验失败。\n");
        return;
    }
    while (1) {
        input_non_empty("新密码(至少6位): ", newPwd, sizeof(newPwd));
        if ((int)strlen(newPwd) >= MIN_PASSWORD_LEN) break;
        printf("密码过短。\n");
    }
    strncpy(a->data.password, newPwd, sizeof(a->data.password) - 1);
    a->data.password[sizeof(a->data.password) - 1] = '\0';
    autosave_default();
    printf("中介密码已更新，请使用新密码登录。\n");
}

static void recover_tenant_password(void) {
    int mode = input_int("找回方式 1租客ID 2手机号 0返回: ", 0, 2);
    char idCard[20];
    char newPwd[32];
    TenantNode *t = NULL;
    if (!reload_database_for_sync()) {
        printf("数据同步失败，找回已取消。\n");
        return;
    }
    if (mode == 0) return;
    if (mode == 1) {
        int id = input_int("租客ID(0返回): ", 0, TENANT_ID_MAX);
        if (id == 0) return;
        t = find_tenant(id);
        if (!t) {
            /* 并发场景下做一次强制重同步再重查，降低刚注册后瞬时查不到的概率 */
            if (reload_database_for_sync()) {
                t = find_tenant(id);
            }
        }
    } else {
        char phone[20];
        printf("注册手机号(输入0返回): ");
        read_line(phone, sizeof(phone));
        if (!phone[0]) {
            printf("不能为空。\n");
            return;
        }
        if (strcmp(phone, "0") == 0) return;
        t = find_tenant_by_phone(phone);
    }
    if (!t) {
        printf("租客不存在。请确认输入的是注册成功时显示的租客ID，或改用手机号找回。\n");
        return;
    }
    printf("身份证(18位,输入0返回): ");
    read_line(idCard, sizeof(idCard));
    if (!idCard[0]) {
        printf("不能为空。\n");
        return;
    }
    if (strcmp(idCard, "0") == 0) return;
    if (strcmp(idCard, t->data.idCard) != 0) {
        printf("身份证校验失败。\n");
        return;
    }
    while (1) {
        input_non_empty("新密码(至少6位): ", newPwd, sizeof(newPwd));
        if ((int)strlen(newPwd) >= MIN_PASSWORD_LEN) break;
        printf("密码过短。\n");
    }
    strncpy(t->data.password, newPwd, sizeof(t->data.password) - 1);
    t->data.password[sizeof(t->data.password) - 1] = '\0';
    autosave_default();
    printf("租客密码已更新，请使用新密码登录。\n");
}

static void main_menu(void) {
    BackContext ctx;
    BackContext *prev = g_back_ctx;
    g_back_ctx = &ctx;
    ctx.active = 1;
    if (setjmp(ctx.env) != 0) {
        ui_warn("已返回上一级（主菜单）。");
    }
    int ch;
    while (1) {
        platform_clear_screen();
        ui_banner("租房管理系统 (跨平台版)");
        printf("提示: 任意输入处可用 # 回退上一级；-1 在当前不作为有效值时也可回退。\n");
        printf("1. 管理员登录\n2. 中介登录\n3. 租客登录\n4. 租客注册\n5. 保存数据\n6. 从默认文件恢复\n7. 管理员密码找回(已禁用)\n8. 中介密码找回\n9. 租客密码找回\n10. 生成演示数据\n0. 退出系统\n");
        ch = input_int("请选择: ", 0, 10);
        if (ch == 0) {
            ui_info("系统已退出。");
            g_back_ctx = prev;
            return;
        }
        if (ch == 1) login_admin();
        else if (ch == 2) login_agent();
        else if (ch == 3) login_tenant();
        else if (ch == 4) tenant_register();
        else if (ch == 5) {
            if (!reload_database_for_sync()) {
                ui_error("数据同步失败，未执行保存。");
            } else {
                save_to_file(g_data_file);
            }
        }
        else if (ch == 6) {
            if (load_from_file(g_data_file)) {
                update_expired_rentals();
                ui_success("恢复成功。");
            } else {
                ui_error("恢复失败。");
            }
        } else if (ch == 7) recover_admin_password();
        else if (ch == 8) recover_agent_password();
        else if (ch == 9) recover_tenant_password();
        else {
            if (!reload_database_for_sync()) {
                ui_error("数据同步失败，无法生成演示数据。");
                continue;
            }
            seed_demo_data();
            autosave_default();
            ui_success("演示数据已生成/补齐。");
        }
    }
}

void rental_system_run(const char *argv0) {
    int upgraded = 0;
    ui_init();
    srand((unsigned int)time(NULL));
    setup_data_file_path(argv0);
    memset(&g_db, 0, sizeof(g_db));
    if (data_file_exists(g_data_file) && !load_from_file(g_data_file)) {
        init_defaults(&g_db);
        save_to_file(g_data_file);
    } else if (!data_file_exists(g_data_file)) {
        init_defaults(&g_db);
        save_to_file(g_data_file);
    }
    if (g_db.houseCount == 0 || g_db.tenantCount == 0) {
        seed_demo_data();
        save_to_file(g_data_file);
    }
    upgraded = upgrade_demo_agent_id_cards();
    if (upgraded) {
        save_to_file(g_data_file);
    }
    update_expired_rentals();
    main_menu();
    clear_all_lists();
}
