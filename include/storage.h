#ifndef STORAGE_H
#define STORAGE_H

#include "rental_system.h"

/*
 * 功能: 将内存中的数据库写入文件
 * 输入: filename 目标文件路径，db 数据库对象
 * 输出: 1 保存成功，0 保存失败
 */
int storage_save(const char *filename, const Database *db);

/*
 * 功能: 从文件加载数据库到内存
 * 输入: filename 源文件路径，db 待填充数据库对象
 * 输出: 1 加载成功，0 加载失败
 */
int storage_load(const char *filename, Database *db);

#endif
