#ifndef LOGIN_GUARD_H
#define LOGIN_GUARD_H

int login_guard_is_locked(int role, int id);
void login_guard_record_fail(int role, int id, int maxAttempts, int lockSeconds);
void login_guard_record_success(int role, int id);
int login_guard_remaining_attempts(int role, int id, int maxAttempts);

#endif
