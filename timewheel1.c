// timewheel.c
#include "timewheel1.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define WHEEL_SIZE 65536 // 时间轮槽位数（最大支持 1023ms 的定时器）

// 每个槽是一个单向链表
static timer_node_t *wheel[WHEEL_SIZE];

// 当前时间 tick（从 0 开始，每毫秒 +1）
static uint32_t current_tick = 0;

void init_timer(void)
{
    memset(wheel, 0, sizeof(wheel));
    current_tick = 0;
}

timer_node_t *add_timer(int delay_ms, timer_callback_t callback, int id)
{
    if (delay_ms <= 0)
    {
        // 立即执行（不加入时间轮）
        timer_node_t tmp = {.callback = callback, .id = id};
        callback(&tmp);
        return NULL;
    }

    timer_node_t *node = (timer_node_t *)malloc(sizeof(timer_node_t));
    if (!node)
        return NULL;

    node->expire = current_tick + (uint32_t)delay_ms;
    node->callback = callback;
    node->id = id;
    node->next = NULL;

    uint32_t slot = node->expire % WHEEL_SIZE;

    // 头插法插入链表
    node->next = wheel[slot];
    wheel[slot] = node;

    return node;
}

void expire_timer(void)
{
    uint32_t slot = current_tick % WHEEL_SIZE;

    // 取出当前槽的所有节点并清空槽
    timer_node_t *head = wheel[slot];
    wheel[slot] = NULL;

    // 执行所有到期定时器
    while (head)
    {
        timer_node_t *next = head->next;
        head->callback(head); // 调用用户回调
        free(head);
        head = next;
    }

    current_tick++; // 推进时间
}

void clear_timer(void)
{
    for (int i = 0; i < WHEEL_SIZE; i++)
    {
        timer_node_t *node = wheel[i];
        while (node)
        {
            timer_node_t *next = node->next;
            free(node);
            node = next;
        }
        wheel[i] = NULL;
    }
    current_tick = 0;
}