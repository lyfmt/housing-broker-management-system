/*
 * 文件: data_path_utils.c
 * 定义内容: 数据文件路径探测、归一化与构建目录映射逻辑。
 * 后续用途: 确保不同操作系统和不同启动目录下都能稳定定位数据文件。
 */
#include "data_path_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define PATH_SEPARATOR "\\"
#else
#define PATH_SEPARATOR "/"
#endif

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

int data_path_file_exists(const char *file) {
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

static void resolve_path_to_absolute(char *path, size_t size) {
    char cwd[512];
    char absolute[512];

    if (!path || size == 0) return;
    if (is_absolute_path(path)) return;
    if (!getcwd(cwd, sizeof(cwd))) return;

    if (snprintf(absolute, sizeof(absolute), "%s%s%s", cwd, PATH_SEPARATOR, path)
        >= (int)sizeof(absolute)) {
        return;
    }

    strncpy(path, absolute, size - 1);
    path[size - 1] = '\0';
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

static void remap_build_dir_data_path(char *path, size_t size, const char *defaultFileName) {
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
    (void)size;
    normalize_simple_path(path);
    for (i = 0; i < (int)(sizeof(suffixes) / sizeof(suffixes[0])); ++i) {
        const char *suf = suffixes[i];
        size_t gl, su;
        if (!str_ends_with(path, suf)) continue;
        gl = strlen(path);
        su = strlen(suf);
        path[gl - su] = '\0';
        strncat(path, "/", 511 - strlen(path));
        strncat(path, defaultFileName, 511 - strlen(path));
        return;
    }
}

void data_path_setup_from_argv(char *outPath, size_t outSize, const char *argv0, const char *defaultFileName) {
    const char *slash1;
    const char *slash2;
    const char *last;
    size_t dirLen;

    if (!outPath || outSize == 0 || !defaultFileName || !defaultFileName[0]) return;

    if (!argv0 || !argv0[0]) {
        strncpy(outPath, defaultFileName, outSize - 1);
        outPath[outSize - 1] = '\0';
    } else {
        slash1 = strrchr(argv0, '/');
        slash2 = strrchr(argv0, '\\');
        last = slash1;
        if (!last || (slash2 && slash2 > last)) last = slash2;
        if (!last) {
            strncpy(outPath, defaultFileName, outSize - 1);
            outPath[outSize - 1] = '\0';
        } else {
            dirLen = (size_t)(last - argv0 + 1);
            if (dirLen + strlen(defaultFileName) >= outSize) {
                strncpy(outPath, defaultFileName, outSize - 1);
                outPath[outSize - 1] = '\0';
            } else {
                memcpy(outPath, argv0, dirLen);
                outPath[dirLen] = '\0';
                strncat(outPath, defaultFileName, outSize - strlen(outPath) - 1);
            }
        }
    }

    resolve_path_to_absolute(outPath, outSize);
    remap_build_dir_data_path(outPath, outSize, defaultFileName);
}
