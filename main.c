// main.c
#include <stdio.h>
#include <unistd.h>
#include "timewheel1.h"

void on_timer(void *arg)
{
    timer_node_t *node = (timer_node_t *)arg;
    printf("Timer expired! ID = %d\n", node->id);

    // 示例：ID=1 的定时器每 500ms 循环一次
    if (node->id == 1)
    {
        add_timer(500, on_timer, 1);
    }
}

int main()
{
    init_timer();

    // 添加测试定时器
    add_timer(1000, on_timer, 1); // 1秒后开始，之后每500ms一次
    add_timer(2500, on_timer, 2); // 2.5秒后触发一次
    add_timer(4000, on_timer, 3); // 4秒后触发一次

    printf("Running timer system for 5 seconds...\n");

    // 主循环：每 1ms 推进一次时间轮
    for (int i = 0; i < 5000; i++)
    {
        expire_timer();
        usleep(1000); // 休眠 1 毫秒（≈1ms）
    }

    clear_timer();
    printf("Done.\n");
    return 0;
}