#ifndef LOGIN_GUARD_H
#define LOGIN_GUARD_H

/*
 * 文件: login_guard.h
 * 定义内容: 登录失败计数、锁定检测与剩余尝试次数查询接口。
 * 后续用途: 管理员/中介/租客登录流程共享同一风控入口。
 */

/* 当前账号是否处于锁定状态。 */
int login_guard_is_locked(int role, int id);
/* 记录一次登录失败并按阈值触发锁定。 */
void login_guard_record_fail(int role, int id, int maxAttempts, int lockSeconds);
/* 记录登录成功并清空失败计数/锁定状态。 */
void login_guard_record_success(int role, int id);
/* 查询账号剩余可尝试次数。 */
int login_guard_remaining_attempts(int role, int id, int maxAttempts);

#endif
