/*
 * 文件: login_guard.c
 * 定义内容: 登录失败计数与锁定控制，实现防暴力尝试的轻量防护策略。
 * 后续用途: 所有角色登录入口统一复用，后续可扩展更细粒度风控策略。
 */
#include "login_guard.h"

#include <stdio.h>
#include <time.h>

typedef struct {
    int role;
    int id;
    int failed;
    time_t lockedUntil;
} LoginGuard;

static LoginGuard g_guards[1024];
static int g_guardCount = 0;

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

int login_guard_is_locked(int role, int id) {
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

void login_guard_record_fail(int role, int id, int maxAttempts, int lockSeconds) {
    LoginGuard *g = get_login_guard(role, id);
    if (!g) return;
    g->failed++;
    if (g->failed >= maxAttempts) {
        g->lockedUntil = time(NULL) + lockSeconds;
        g->failed = 0;
        printf("失败次数过多，账号锁定15分钟。\n");
    }
}

void login_guard_record_success(int role, int id) {
    LoginGuard *g = get_login_guard(role, id);
    if (!g) return;
    g->failed = 0;
    g->lockedUntil = 0;
}

int login_guard_remaining_attempts(int role, int id, int maxAttempts) {
    LoginGuard *g = get_login_guard(role, id);
    if (!g) return 0;
    return maxAttempts - g->failed;
}
