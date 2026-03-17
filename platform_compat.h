#ifndef PLATFORM_COMPAT_H
#define PLATFORM_COMPAT_H

#ifdef _WIN32
#include <windows.h>
#define PATH_SEPARATOR "\\"
static inline void platform_sleep_ms(unsigned int ms) { Sleep(ms); }
static inline void platform_setup_utf8_console(void) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}
static inline int platform_enable_virtual_terminal(void) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (hOut == INVALID_HANDLE_VALUE) return 0;
    if (!GetConsoleMode(hOut, &mode)) return 0;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hOut, mode)) return 0;
    return 1;
}
#else
#include <sys/select.h>
#include <unistd.h>
#define PATH_SEPARATOR "/"
static inline void platform_sleep_ms(unsigned int ms) {
    struct timeval tv;
    tv.tv_sec = (long)(ms / 1000u);
    tv.tv_usec = (int)((ms % 1000u) * 1000u);
    (void)select(0, NULL, NULL, NULL, &tv);
}
static inline void platform_setup_utf8_console(void) { }
static inline int platform_enable_virtual_terminal(void) { return 1; }
#endif

static inline void platform_clear_screen(void) {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

static inline int get_terminal_width(void) {
#ifdef _WIN32
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (hStdout == INVALID_HANDLE_VALUE) return 80;
    if (GetConsoleScreenBufferInfo(hStdout, &csbi)) {
        int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        return (width > 0) ? width : 80;
    }
    return 80;
#else
    #include <sys/ioctl.h>
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1 && ws.ws_col > 0) {
        return ws.ws_col;
    }
    return 80;
#endif
}

#endif
