#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h> // ← 必须！用于 printf
#include <stdlib.h>

#define WHEEL_SIZE 65536

typedef void (*callback)(void *);

typedef struct TimeWheelNode
{
    callback func;
    struct TimeWheelNode* next;
    void* args;
    uint32_t expire;
} TimeWheelNode;

//推进时间轮
void expireTimer(TimeWheelNode **list, int current)
{
    int pos = current % WHEEL_SIZE;
    TimeWheelNode *head = list[pos];
    list[pos] = NULL; // ← 关键：先清空槽位！

    // 现在安全地遍历 head 链表，无论回调做什么，都不会影响当前处理
    while (head != NULL)
    {
        TimeWheelNode *next = head->next;
        head->func(head->args);
        free(head);
        head = next;
    }
}

// 加入任务
void addNewNode(TimeWheelNode **list, callback func, uint32_t delay, void *args, int current)
{   
    if(delay<=0)
    {
        func(args);
        return;
    }

    int pos = (current + delay) % WHEEL_SIZE;
    TimeWheelNode *node = (TimeWheelNode*)malloc(sizeof(TimeWheelNode));
    node->expire = delay+current;
    node->func = func;
    node->next = NULL;
    node->args = args;

    if((list[pos]) == NULL)
    {
        list[pos] = node;
    }
    else
    {
        node->next = list[pos];
        list[pos] = node;
    }

}


//清空时间轮
void clearTimeWheel(TimeWheelNode **list)
{
    for(int i = 0;i<WHEEL_SIZE;i++)
    {
        while(list[i]!=NULL)
        {
            TimeWheelNode *t = list[i]->next;
            free(list[i]);
            list[i]=t;
        }
    }
}

void on_timer(void *arg)
{
    int *value = (int*)arg;
    printf("Timer expired! value = %d\n", *value);
}

int main()
{

    int current = 0;
    TimeWheelNode* wheel[WHEEL_SIZE];
    memset(wheel, 0, sizeof(wheel));

    // 添加测试定时器
    int val1 = 1, val2 = 2, val3 = 3;
    addNewNode(wheel, on_timer, 1000, &val1, current);
    addNewNode(wheel, on_timer, 4000, &val2, current); 
    addNewNode(wheel, on_timer, 5000, &val3, current); 

    printf("Running timer system for 5 seconds...\n");

    // 主循环：每 1ms 推进一次时间轮
    for (int i = 0; i < 6000; i++)
    {
        expireTimer(wheel,current);
        current++;
        usleep(1000); // 休眠 1 毫秒（≈1ms）
    }

    clearTimeWheel(wheel);
    printf("Done.\n");
    return 0;
}