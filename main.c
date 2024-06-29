#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include "ThreadPool.h"

void taskFunc(void *arg) {
    int num = *(int *) arg;
    printf("thread %ld is working, number = %d\n", pthread_self(), num);
    sleep(1);
}

int main(void) {
    // 创建线程池
    ThreadPool *threadPool = createThreadPool(3, 10, 100);

    for (int i = 0; i < 100; i++) {
        int *num = (int *) malloc(sizeof(int));
        *num = i + 100;
        addTask(threadPool, taskFunc, num);
    }

    sleep(5);

    destroyThreadPool(threadPool);
    return 0;
}
