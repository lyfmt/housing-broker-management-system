#ifndef UI_UTILS_H
#define UI_UTILS_H

#include <stdio.h>
#include "platform_compat.h"

#define UI_PAGE_SIZE 10

#define UI_COLOR_RESET  "\x1b[0m"
#define UI_COLOR_RED    "\x1b[31m"
#define UI_COLOR_GREEN  "\x1b[32m"
#define UI_COLOR_YELLOW "\x1b[33m"
#define UI_COLOR_BLUE   "\x1b[34m"
#define UI_COLOR_CYAN   "\x1b[36m"

static int g_ui_color_enabled = 1;

static inline void ui_init(void) {
    platform_setup_utf8_console();
    g_ui_color_enabled = platform_enable_virtual_terminal();
}

static inline void ui_color_print(const char *color, const char *prefix, const char *msg) {
    if (color && g_ui_color_enabled) printf("%s", color);
    if (prefix) printf("%s", prefix);
    if (msg) printf("%s", msg);
    if (color && g_ui_color_enabled) printf("%s", UI_COLOR_RESET);
    printf("\n");
}

static inline void ui_info(const char *msg) { ui_color_print(UI_COLOR_BLUE, "[INFO] ", msg); }
static inline void ui_success(const char *msg) { ui_color_print(UI_COLOR_GREEN, "[OK] ", msg); }
static inline void ui_warn(const char *msg) { ui_color_print(UI_COLOR_YELLOW, "[WARN] ", msg); }
static inline void ui_error(const char *msg) { ui_color_print(UI_COLOR_RED, "[ERROR] ", msg); }

static inline void ui_line(char ch, int width) {
    int i;
    for (i = 0; i < width; ++i) putchar(ch);
    putchar('\n');
}

static inline void ui_banner(const char *title) {
    ui_line('=', 56);
    printf("%s\n", title);
    ui_line('=', 56);
}

static inline void ui_section(const char *title) {
    ui_line('-', 56);
    printf("%s\n", title);
    ui_line('-', 56);
}

static inline int ui_page_break_if_needed(int shownCount) {
    char cbuf[8];
    if (shownCount > 0 && shownCount % UI_PAGE_SIZE == 0) {
        printf("-- 已显示%d条，按回车继续，输入q返回 --", shownCount);
        if (fgets(cbuf, sizeof(cbuf), stdin) != NULL) {
            if (cbuf[0] == 'q' || cbuf[0] == 'Q') {
                return 0;
            }
        }
    }
    return 1;
}

static inline void ui_loading(const char *message) {
    int i;
    printf("%s", message);
    fflush(stdout);
    for (i = 0; i < 3; ++i) {
        printf(".");
        fflush(stdout);
        platform_sleep_ms(220);
    }
    printf("\n");
}

#endif
