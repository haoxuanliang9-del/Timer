#include <stdio.h>
#include <unistd.h>

#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include "timewheel.h"

struct context {
	int quit;
    int thread;
};

struct thread_param {
	struct context *ctx;
	int id;
};

static struct context ctx = {0};

void do_timer(timer_node_t *node) {
    printf("do_timer expired:%d - thread-id:%d\n", node->expire, node->id);
    add_timer(100, do_timer, node->id);
}

void do_clock(timer_node_t *node) {
    static int time;
    time ++;
    printf("---time = %d ---\n", time);
    add_timer(100, do_clock, node->id);
}

void* thread_worker(void *p) {
	struct thread_param *tp = p;
	int id = tp->id;
    struct context *ctx = tp->ctx;
    int expire = rand() % 200; 
    add_timer(expire, do_timer, id);
	while (!ctx->quit) {
        usleep(1000);
    }
    printf("thread_worker:%d exit!\n", id);
    return NULL;
}

void do_quit(timer_node_t * node) {
    ctx.quit = 1;
}

int main() {
    srand(time(NULL));//srand生成随机数种子，time返回当前时间，时间不同保证随机数种子每次不同
    ctx.thread = 2;
    pthread_t pid[ctx.thread];

    init_timer();
    add_timer(6000, do_quit, 100);
    add_timer(0, do_clock, 100);
    struct thread_param task_thread_p[ctx.thread];
    int i;
    for (i = 0; i < ctx.thread; i++) {
        task_thread_p[i].id = i;
        task_thread_p[i].ctx = &ctx;
        if (pthread_create(&pid[i], NULL, thread_worker, &task_thread_p[i])) {
            fprintf(stderr, "create thread failed\n");
            exit(1);
        }
    }

    while (!ctx.quit) {
        expire_timer();
        usleep(250);
    }
    clear_timer();
    for (i = 0; i < ctx.thread; i++) {
		pthread_join(pid[i], NULL);
    }
    printf("all thread is closed\n");
    return 0;
}

// gcc tw-timer.c timewheel.c -o tw -I./ -lpthread 