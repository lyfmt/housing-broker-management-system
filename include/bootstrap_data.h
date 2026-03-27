#ifndef BOOTSTRAP_DATA_H
#define BOOTSTRAP_DATA_H

/*
 * 文件: bootstrap_data.h
 * 定义内容: 默认配置与演示数据初始化相关接口声明。
 * 后续用途: 由系统启动与管理员“生成演示数据”功能统一调用，保证数据基线可恢复。
 */

#include "rental_system.h"

/* 初始化基础默认值（管理员密码、分类字典等）。 */
void bootstrap_init_defaults(Database *db);
/* 向数据库补齐演示账号、房源、看房、租约数据。 */
void bootstrap_seed_demo_data(Database *db);
/* 升级历史演示数据中的中介身份证缺失问题。 */
int bootstrap_upgrade_demo_agent_id_cards(Database *db);

#endif
