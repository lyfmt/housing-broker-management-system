#ifndef PASSWORD_UTILS_H
#define PASSWORD_UTILS_H

/*
 * 文件: password_utils.h
 * 定义内容: 密码存储、校验、数据库口令迁移与临时密码生成接口。
 * 后续用途: 认证流程统一依赖该模块，便于后续替换加密策略。
 */

#include <stddef.h>

#include "rental_system.h"

/* 将输入明文按当前策略写入 dest（哈希化存储）。 */
void password_store(char *dest, size_t size, const char *password);
/* 校验输入明文是否与已存储口令匹配。 */
int password_verify(const char *stored, const char *input);
/* 扫描数据库并迁移旧版明文口令到哈希口令。 */
int normalize_database_passwords(Database *db);
/* 生成一次性临时密码。 */
void generate_temporary_password(char *out, size_t size);

#endif
