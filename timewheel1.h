// timewheel.h
#ifndef TIMEWHEEL_H
#define TIMEWHEEL_H

#include <stdint.h>

// 定时器回调函数类型
typedef void (*timer_callback_t)(void *node);

// 定时器节点结构
typedef struct timer_node
{
    struct timer_node *next;   // 链表指针
    uint32_t expire;           // 绝对到期 tick（单位：毫秒）
    timer_callback_t callback; // 回调函数
    int id;                    // 用户自定义 ID
} timer_node_t;

// 初始化时间轮（必须最先调用）
void init_timer(void);

// 添加定时器：delay_ms 毫秒后触发
// 注意：delay_ms 必须 > 0；若 <=0 则立即执行（不加入轮子）
timer_node_t *add_timer(int delay_ms, timer_callback_t callback, int id);

// 推进时间轮：应每 1 毫秒调用一次（例如在主循环中 usleep(1000) 后调用）
void expire_timer(void);

// 清理所有未触发的定时器（程序退出前调用）
void clear_timer(void);

#endif