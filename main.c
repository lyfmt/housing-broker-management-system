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
