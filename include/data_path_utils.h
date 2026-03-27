#ifndef DATA_PATH_UTILS_H
#define DATA_PATH_UTILS_H

/*
 * 文件: data_path_utils.h
 * 定义内容: 数据文件路径处理接口（存在性检测与启动路径解析）。
 * 后续用途: 避免因工作目录变化导致读取/保存到错误数据文件。
 */

#include <stddef.h>

/* 检测给定数据文件是否存在。 */
int data_path_file_exists(const char *file);
/* 根据可执行路径推导并规范化数据文件路径。 */
void data_path_setup_from_argv(char *outPath, size_t outSize, const char *argv0, const char *defaultFileName);

#endif
