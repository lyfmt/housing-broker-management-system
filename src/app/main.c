/*
 * 文件: main.c
 * 定义内容: 程序进程入口，仅负责将控制权交给业务主流程。
 * 后续用途: 统一启动点，便于后续接入启动参数扩展、启动前初始化或集成测试入口。
 */
#include "rental_system.h"

/*
 * 程序入口函数
 * 输入: argc/argv 为命令行参数
 * 输出: 返回进程退出码，0 表示正常结束
 */
int main(int argc, char *argv[]) {
    (void)argc;
    rental_system_run(argv[0]);
    return 0;
}
