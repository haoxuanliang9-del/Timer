#define _POSIX_C_SOURCE 199309L

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

// 时间轮层级参数（2 的幂）
#define TVR_BITS 8 // L1: 2^8 = 256 slots
#define TVN_BITS 6 // L2~L5: 2^6 = 64 slots

#define TVR_SIZE (1U << TVR_BITS) // 256
#define TVN_SIZE (1U << TVN_BITS) // 64

#define TVR_MASK (TVR_SIZE - 1) // 0xFF
#define TVN_MASK (TVN_SIZE - 1) // 0x3F

// 最大支持延迟：2^(8 + 4*6) = 2^32 = 4294967296，但 uint64_t 最大为 4294967295
#define MAX_SUPPORTED_DELAY (UINT32_MAX)

typedef void (*callback)(void *);


typedef struct TimeWheelNode
{
    callback func;
    struct TimeWheelNode *next;
    void *args;
    uint64_t expire;
    bool active;
    
} TimeWheelNode;

typedef struct Wheel
{
    TimeWheelNode *wheelL1[TVR_SIZE]; // 256
    TimeWheelNode *wheelL2[TVN_SIZE]; // 64
    TimeWheelNode *wheelL3[TVN_SIZE];
    TimeWheelNode *wheelL4[TVN_SIZE];
    TimeWheelNode *wheelL5[TVN_SIZE];
    uint64_t time;
} Wheel;

uint64_t get_monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

void initWheel(Wheel *wheel)
{
    memset(wheel, 0, sizeof(Wheel));
    wheel->time = get_monotonic_ms();
}

void insertTimer(TimeWheelNode **slot, TimeWheelNode *node)
{
    node->next = *slot;
    *slot = node;
}

void reAddTimer(Wheel *wheel, TimeWheelNode *node)
{

    if(node->active == false)
    {
        free(node);
        return;
    }
    uint64_t current = get_monotonic_ms();
    uint64_t expire = node->expire;
    uint64_t delay = expire - current;

    int level;
    int pos;
    if (delay < TVR_SIZE)
    {
        level = 1;
        pos = expire & TVR_MASK;
        insertTimer(&wheel->wheelL1[pos], node);
    }
    else if (delay < (1U << (TVR_BITS + TVN_BITS)))
    {
        level = 2;
        pos = (expire >> TVR_BITS) & TVN_MASK;
        insertTimer(&wheel->wheelL2[pos], node);
    }
    else if (delay < (1U << (TVR_BITS + 2 * TVN_BITS)))
    {
        level = 3;
        pos = (expire >> (TVR_BITS + TVN_BITS)) & TVN_MASK;
        insertTimer(&wheel->wheelL3[pos], node);
    }
    else if (delay < (1U << (TVR_BITS + 3 * TVN_BITS)))
    {
        level = 4;
        pos = (expire >> (TVR_BITS + 2 * TVN_BITS)) & TVN_MASK;
        insertTimer(&wheel->wheelL4[pos], node);
    }
    else if (delay <= MAX_SUPPORTED_DELAY)
    {
        level = 5;
        pos = (expire >> (TVR_BITS + 3 * TVN_BITS)) & TVN_MASK;
        insertTimer(&wheel->wheelL5[pos], node);
    }
    else
    {
        // 超出支持范围，丢弃
        free(node);
        return;
    }
}

// 用于 L2~L5：只 cascade，不执行
static void cascadeLevel(TimeWheelNode **wheel, int size, int level, uint64_t last, uint64_t now, Wheel *w)
{

    int begin,end,round;
    switch(level)
    {
        case 5:
        {
            begin = (last >> (TVR_BITS + 3 * TVN_BITS)) & TVN_MASK;
            end = (now >> (TVR_BITS + 3 * TVN_BITS)) & TVN_MASK;
            round = TVR_SIZE * TVN_SIZE * TVN_SIZE * TVN_SIZE * TVN_SIZE;
            break;
        }
        case 4:
        {
            begin = (last >> (TVR_BITS + 2 * TVN_BITS)) & TVN_MASK;
            end = (now >> (TVR_BITS + 2 * TVN_BITS)) & TVN_MASK;
            round = TVR_SIZE * TVN_SIZE * TVN_SIZE * TVN_SIZE;
            break;
        }
        case 3:
        {
            begin = (last >> (TVR_BITS + TVN_BITS)) & TVN_MASK;
            end = (now >> (TVR_BITS + TVN_BITS)) & TVN_MASK;
            round = TVR_SIZE * TVN_SIZE * TVN_SIZE;
            break;
        }
        case 2:
        {
            begin = (last >> TVR_BITS) & TVN_MASK;
            end = (now >> TVR_BITS) & TVN_MASK;
            round = TVR_SIZE * TVN_SIZE;
            break;
        }
        default:
            break;
    }

    if(now - last >= round)
    {
        for (int i = 0; i < size; ++i)
        {
            TimeWheelNode *head = wheel[i];
            wheel[i] = NULL;
            while (head)
            {
                TimeWheelNode *next = head->next;
                reAddTimer(w, head); // 重新插入时间轮
                head = next;
            }
        }
    }
    else
    {
        if (end >= begin)
        {
            for (int i = begin + 1; i <= end; ++i)
            {
                TimeWheelNode *head = wheel[i];
                wheel[i] = NULL;
                while (head)
                {
                    TimeWheelNode *next = head->next;
                    reAddTimer(w, head); // 重新插入时间轮
                    head = next;
                }
            }
        }
        else
        {
            for (int i = begin + 1; i < size; ++i)
            {
                TimeWheelNode *head = wheel[i];
                wheel[i] = NULL;
                while (head)
                {
                    TimeWheelNode *next = head->next;
                    reAddTimer(w, head);
                    head = next;
                }
            }
            for (int i = 0; i <= end; ++i)
            {
                TimeWheelNode *head = wheel[i];
                wheel[i] = NULL;
                while (head)
                {
                    TimeWheelNode *next = head->next;
                    reAddTimer(w, head);
                    head = next;
                }
            }
        }
    }

    
}

// 用于 L1：执行已到期的
static void execTimerL1(TimeWheelNode **wheel, int size, uint64_t last, uint64_t now)
{
    int begin = last & TVR_MASK;
    int end = now & TVR_MASK;

    if (now - last >= size)
    {
        for (int i = 0; i < size; ++i)
        {
            TimeWheelNode **pp = &wheel[i];
            while (*pp)
            {
                TimeWheelNode *node = *pp;
                if (node->expire <= now)
                {
                    if (node->active)
                        node->func(node->args);
                    *pp = node->next;
                    free(node);
                }
                else
                {
                    pp = &node->next;
                }
            }
        }
    }
    else
    {
        if (end >= begin)
        {
            for (int i = begin + 1; i <= end; ++i)
            {
                TimeWheelNode **pp = &wheel[i];
                while (*pp)
                {
                    TimeWheelNode *node = *pp;
                    if (node->expire <= now)
                    {
                        if (node->active)
                            node->func(node->args);
                        *pp = node->next;
                        free(node);
                    }
                    else
                    {
                        pp = &node->next;
                    }
                }
            }
        }
        else
        {
            for (int i = begin + 1; i < size; ++i)
            {
                TimeWheelNode **pp = &wheel[i];
                while (*pp)
                {
                    TimeWheelNode *node = *pp;
                    if (node->expire <= now)
                    {
                        if (node->active)
                            node->func(node->args);
                        *pp = node->next;
                        free(node);
                    }
                    else
                    {
                        pp = &node->next;
                    }
                }
            }
            for (int i = 0; i <= end; ++i)
            {
                TimeWheelNode **pp = &wheel[i];
                while (*pp)
                {
                    TimeWheelNode *node = *pp;
                    if (node->expire <= now)
                    {
                        if (node->active)
                            node->func(node->args);
                        *pp = node->next;
                        free(node);
                    }
                    else
                    {
                        pp = &node->next;
                    }
                }
            }
        }
    }
    
}

void expireTimer(Wheel *w)
{
    uint64_t current = get_monotonic_ms();
    if (current <= w->time)
        return;

    // L5
    cascadeLevel(w->wheelL5, TVN_SIZE, 5, w->time, current, w);

    // L4
    cascadeLevel(w->wheelL4, TVN_SIZE, 4, w->time, current, w);

    // L3
    cascadeLevel(w->wheelL3, TVN_SIZE, 3, w->time, current, w);

    // L2
    cascadeLevel(w->wheelL2, TVN_SIZE, 2, w->time, current, w);

    //L1
    execTimerL1(w->wheelL1, TVR_SIZE, w->time, current);

    w->time = current;
}

TimeWheelNode* addNewTimer(Wheel *wheel, callback func, uint64_t delay, void *args)
{
    if (delay == 0)
    {
        func(args);
        return NULL;
    }

    uint64_t current = get_monotonic_ms();
    // 防止溢出：current + delay 可能回绕，但 expire 仍有效（只要 delay <= MAX）
    uint64_t expire = current + delay; // unsigned arithmetic is well-defined

    TimeWheelNode *node = malloc(sizeof(TimeWheelNode));
    if (!node)
    {
        perror("malloc");
        return NULL;
    }
    node->expire = expire;
    node->func = func;
    node->args = args;
    node->active = true;

    int level;
    int pos;
    if (delay < TVR_SIZE)
    {   level = 1;
        pos = expire & TVR_MASK;
        insertTimer(&wheel->wheelL1[pos], node);
    }
    else if (delay < (1U << (TVR_BITS + TVN_BITS)))
    {
        level = 2;
        pos = (expire >> TVR_BITS) & TVN_MASK;
        insertTimer(&wheel->wheelL2[pos], node);
    }
    else if (delay < (1U << (TVR_BITS + 2 * TVN_BITS)))
    {
        level = 3;
        pos = (expire >> (TVR_BITS + TVN_BITS)) & TVN_MASK;
        insertTimer(&wheel->wheelL3[pos], node);
    }
    else if (delay < (1U << (TVR_BITS + 3 * TVN_BITS)))
    {
        level = 4;
        pos = (expire >> (TVR_BITS + 2 * TVN_BITS)) & TVN_MASK;
        insertTimer(&wheel->wheelL4[pos], node);
    }
    else if (delay <= MAX_SUPPORTED_DELAY)
    {
        level = 5;
        pos = (expire >> (TVR_BITS + 3 * TVN_BITS)) & TVN_MASK;
        insertTimer(&wheel->wheelL5[pos], node);
    }
    else
    {
        // 超出支持范围，丢弃
        free(node);
        return NULL;
    }

    return node;
}

static void clearList(TimeWheelNode **head)
{
    TimeWheelNode *curr = *head;
    while (curr)
    {
        TimeWheelNode *next = curr->next;
        free(curr);
        curr = next;
    }
    *head = NULL;
}

void clearTimeWheel(Wheel *w)
{
    for (int i = 0; i < TVR_SIZE; i++)
        clearList(&w->wheelL1[i]);
    for (int i = 0; i < TVN_SIZE; i++)
    {
        clearList(&w->wheelL2[i]);
        clearList(&w->wheelL3[i]);
        clearList(&w->wheelL4[i]);
        clearList(&w->wheelL5[i]);
    }
}

void cancelTimer(TimeWheelNode* t)
{
    if(t&&t->active==true)
        t->active = false;
}

void on_timer(void *arg)
{
    int *value = (int *)arg;
    printf("Timer expired! value = %d\n", *value);
    free(value); // 安全释放动态分配的参数
}

int main()
{
    printf("Current time: %llu ms\n", (unsigned long long)get_monotonic_ms());
    Wheel wheel;
    initWheel(&wheel);

    int *val1 = malloc(sizeof(int));
    *val1 = 1;
    int *val2 = malloc(sizeof(int));
    *val2 = 2;
    int *val3 = malloc(sizeof(int));
    *val3 = 3;

    TimeWheelNode *t1 = addNewTimer(&wheel, on_timer, 1000, val1);
    TimeWheelNode *t2 = addNewTimer(&wheel, on_timer, 4000, val2);
    TimeWheelNode *t3 = addNewTimer(&wheel, on_timer, 5000, val3);

    cancelTimer(t2);

    printf("Running timer system for ~6 seconds...\n");

    for (int i = 0; i < 6; ++i)
    {
        struct timespec ts = {.tv_sec = 1, .tv_nsec = 0};
        nanosleep(&ts, NULL); // sleep 1 second
        expireTimer(&wheel);  // 检查是否到期
    }

    clearTimeWheel(&wheel);
    printf("Done.\n");
    return 0;
}