#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// 时间轮层级参数（2 的幂）
#define TVR_BITS 8 // L1: 2^8 = 256 slots
#define TVN_BITS 6 // L2~L5: 2^6 = 64 slots

#define TVR_SIZE (1U << TVR_BITS) // 256
#define TVN_SIZE (1U << TVN_BITS) // 64

#define TVR_MASK (TVR_SIZE - 1) // 0xFF
#define TVN_MASK (TVN_SIZE - 1) // 0x3F

// 最大支持延迟：2^(8 + 4*6) = 2^32 = 4294967296，但 uint32_t 最大为 4294967295
#define MAX_SUPPORTED_DELAY (UINT32_MAX)

typedef void (*callback)(void *);


typedef struct TimeWheelNode
{
    callback func;
    struct TimeWheelNode *next;
    void *args;
    uint32_t expire;
    bool active;
    
} TimeWheelNode;

typedef struct Wheel
{
    TimeWheelNode *wheelL1[TVR_SIZE]; // 256
    TimeWheelNode *wheelL2[TVN_SIZE]; // 64
    TimeWheelNode *wheelL3[TVN_SIZE];
    TimeWheelNode *wheelL4[TVN_SIZE];
    TimeWheelNode *wheelL5[TVN_SIZE];
} Wheel;


void initWheel(Wheel *wheel)
{
    memset(wheel, 0, sizeof(Wheel));
}

void insertTimer(TimeWheelNode **slot, TimeWheelNode *node)
{
    node->next = *slot;
    *slot = node;
}

void reAddTimer(Wheel *wheel, TimeWheelNode *node, uint32_t current)
{

    if(node->active == false)
    {
        free(node);
        return;
    }
    uint32_t expire = node->expire;
    uint32_t delay = expire - current;

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

void expireTimer(Wheel *w, uint32_t current)
{
    uint32_t pos0 = current & TVR_MASK;
    uint32_t pos1 = (current >> TVR_BITS) & TVN_MASK;
    uint32_t pos2 = (current >> (TVR_BITS + TVN_BITS)) & TVN_MASK;
    uint32_t pos3 = (current >> (TVR_BITS + 2 * TVN_BITS)) & TVN_MASK;
    uint32_t pos4 = (current >> (TVR_BITS + 3 * TVN_BITS)) & TVN_MASK;

    // Cascade: only when lower wheel wraps around
    if (pos0 == 0)
    {
        // Promote L2[pos1] → L1
        TimeWheelNode *head = w->wheelL2[pos1];
        w->wheelL2[pos1] = NULL;
        while (head)
        {
            TimeWheelNode *next = head->next;
            reAddTimer(w, head, current);
            head = next;
        }

        if (pos1 == 0)
        {
            // Promote L3[pos2] → lower levels
            head = w->wheelL3[pos2];
            w->wheelL3[pos2] = NULL;
            while (head)
            {
                TimeWheelNode *next = head->next;
                reAddTimer(w, head, current);
                head = next;
            }

            if (pos2 == 0)
            {
                head = w->wheelL4[pos3];
                w->wheelL4[pos3] = NULL;
                while (head)
                {
                    TimeWheelNode *next = head->next;
                    reAddTimer(w, head, current);
                    head = next;
                }

                if (pos3 == 0)
                {
                    head = w->wheelL5[pos4];
                    w->wheelL5[pos4] = NULL;
                    while (head)
                    {
                        TimeWheelNode *next = head->next;
                        reAddTimer(w, head, current);
                        head = next;
                    }
                }
            }
        }
    }

    // Execute all timers in L1[current % TVR_SIZE]
    
    TimeWheelNode *head = w->wheelL1[pos0];
    w->wheelL1[pos0] = NULL;
    while (head)
    {
        TimeWheelNode *next = head->next;
        if (head->active == true)
            head->func(head->args);
        free(head);
        head = next;
    }

}

TimeWheelNode* addNewTimer(Wheel *wheel, callback func, uint32_t delay, void *args, uint32_t current)
{
    if (delay == 0)
    {
        func(args);
        return NULL;
    }

    // 防止溢出：current + delay 可能回绕，但 expire 仍有效（只要 delay <= MAX）
    uint32_t expire = current + delay; // unsigned arithmetic is well-defined

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
    uint32_t current = 0;
    Wheel wheel;
    initWheel(&wheel);

    // 动态分配参数，避免悬空指针
    int *val1 = malloc(sizeof(int));
    *val1 = 1;
    int *val2 = malloc(sizeof(int));
    *val2 = 2;
    int *val3 = malloc(sizeof(int));
    *val3 = 3;

    TimeWheelNode *t1 = addNewTimer(&wheel, on_timer, 1000, val1, current);
    TimeWheelNode *t2 = addNewTimer(&wheel, on_timer, 4000, val2, current);
    TimeWheelNode *t3 = addNewTimer(&wheel, on_timer, 5000, val3, current);

    cancelTimer(t2);

    printf("Running timer system for ~6 seconds...\n");

    for (uint32_t i = 0; i < 6000; i++)
    {
        expireTimer(&wheel, current);
        current++;
        usleep(1000); // ~1ms per tick
    }

    clearTimeWheel(&wheel);
    printf("Done.\n");
    return 0;
}