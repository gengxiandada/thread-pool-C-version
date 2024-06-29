//
// Created by 程思浩 on 2024/6/29.
//

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ThreadPool.h"

#define NUM_THREAD_ADD_EVERY_TIME 2
#define NUM_THREAD_DELETE_EVERY_TIME 2

// 任务结构体
typedef struct Task {
    void (*function)(void *arg); // 任务函数指针

    void *arg; // 任务函数实参
} Task;

// 线程池结构体
typedef struct ThreadPool {
    Task *taskQueue; // 任务队列（环形队列）
    int queueCapacity; // 任务队列容量
    int queueSize; // 当前任务数量
    int queueFront; // 队头
    int queueRear; // 队尾

    pthread_t managerID; // 管理者线程ID
    pthread_t *threadIDs; // 工作线程ID数组

    int minNum; // 最小线程数
    int maxNum; // 最大线程数
    int busyNum; // 工作线程数
    int liveNum; // 存活线程数
    int destroyNum; // 需要销毁的线程数

    pthread_mutex_t mutexPool; // 线程池锁
    pthread_mutex_t mutexBusy; // 变量busyNum锁
    pthread_cond_t notFull; // 任务队列是否为满
    pthread_cond_t notEmpty; // 任务队列是否为空

    int shutDown; // 线程池是否关闭
} ThreadPool;

/// 创建线程池
/// \param min 最小线程数
/// \param max 最大线程数
/// \param queueSize 任务队列大小
/// \return
ThreadPool *createThreadPool(int min, int max, int queueSize) {
    // 创建线程池实例
    ThreadPool *threadPool = (ThreadPool *) malloc(sizeof(ThreadPool));
    do {
        if (threadPool == NULL) {
            printf("malloc thread pool fail\n");
            break;
        }

        // 创建工作线程ID数组
        threadPool->threadIDs = (pthread_t *) malloc(sizeof(pthread_t) * max);
        if (threadPool->threadIDs == NULL) {
            printf("malloc threadIDs fail\n");
            break;
        }
        memset(threadPool->threadIDs, 0, sizeof(pthread_t) * max);

        // 初始化线程参数
        threadPool->minNum = min;
        threadPool->maxNum = max;
        threadPool->busyNum = 0;
        threadPool->liveNum = min;
        threadPool->destroyNum = 0;

        // 初始化互斥和条件变量
        if (pthread_mutex_init(&threadPool->mutexPool, NULL) || pthread_mutex_init(&threadPool->mutexBusy, NULL) ||
            pthread_cond_init(&threadPool->notEmpty, NULL) || pthread_cond_init(&threadPool->notFull, NULL)) {
            printf("mutex or condition variables init fail\n");
            break;
        }

        // 初始化任务队列和相关参数
        threadPool->taskQueue = (Task *) malloc(sizeof(Task) * queueSize);
        threadPool->queueCapacity = queueSize;
        threadPool->queueSize = 0;
        threadPool->queueFront = 0;
        threadPool->queueRear = 0;

        threadPool->shutDown = 0;

        // 创建线程
        pthread_create(&threadPool->managerID, NULL, manager, threadPool);
        for (int i = 0; i < min; i++) {
            pthread_create(&threadPool->threadIDs[i], NULL, worker, threadPool);
        }

        return threadPool;
    } while (0);

    // 释放资源
    if (threadPool && threadPool->threadIDs) {
        free(threadPool->threadIDs);
    }
    if (threadPool && threadPool->taskQueue) {
        free(threadPool->taskQueue);
    }
    if (threadPool) {
        free(threadPool);
    }

    return NULL;
}

/// 工作线程的任务函数
/// \param arg
/// \return
void *worker(void *arg) {
    ThreadPool *threadPool = (ThreadPool *) arg;

    while (1) {
        pthread_mutex_lock(&threadPool->mutexPool);
        // 判断线程池是否被关闭
        if (threadPool->shutDown) {
            pthread_mutex_unlock(&threadPool->mutexPool); // 防止死锁
            destroyThread(threadPool);
        }

        // 判断当前任务队列是否为空
        // 使用while循环防止虚假唤醒，即使线程被错误唤醒，条件不满足时会再次进入等待
        while (threadPool->queueSize == 0) {
            // 阻塞工作线程
            pthread_cond_wait(&threadPool->notEmpty, &threadPool->mutexPool);

            // 判断线程是否需被销毁
            if (threadPool->destroyNum > 0) {
                threadPool->destroyNum--;
                if (threadPool->liveNum > threadPool->minNum) {
                    threadPool->liveNum--;
                    pthread_mutex_unlock(&threadPool->mutexPool); // 防止死锁
                    destroyThread(threadPool);
                }
            }
        }

        // 从任务队列中取出一个任务
        Task task;
        task.function = threadPool->taskQueue[threadPool->queueFront].function;
        task.arg = threadPool->taskQueue[threadPool->queueFront].arg;
        threadPool->queueFront = (threadPool->queueFront + 1) % threadPool->queueCapacity;
        threadPool->queueSize--;
        pthread_cond_signal(&threadPool->notFull);
        pthread_mutex_unlock(&threadPool->mutexPool);

        // 执行任务
        pthread_mutex_lock(&threadPool->mutexBusy);
        threadPool->busyNum++;
        pthread_mutex_unlock(&threadPool->mutexBusy);

        printf("thread %ld start working\n", pthread_self());
//        (*task.function)(task.arg);
        task.function(task.arg);
        free(task.arg);
        task.arg = NULL;
        printf("thread %ld end working\n", pthread_self());

        pthread_mutex_lock(&threadPool->mutexBusy);
        threadPool->busyNum--;
        pthread_mutex_unlock(&threadPool->mutexBusy);
    }

    return NULL;
}

/// 管理者线程的任务函数
/// \param arg
/// \return
void *manager(void *arg) {
    ThreadPool *threadPool = (ThreadPool *) arg;

    while (threadPool->shutDown == 0) {
        // 每隔3s检测一次
        sleep(3);

        // 获取线程池中任务的数量和当前存活线程的数量
        pthread_mutex_lock(&threadPool->mutexPool);
        int queueSize = threadPool->queueSize;
        int liveNum = threadPool->liveNum;
        pthread_mutex_unlock(&threadPool->mutexPool);

        // 获取工作线程数量
        pthread_mutex_lock(&threadPool->mutexBusy);
        int busyNum = threadPool->busyNum;
        pthread_mutex_unlock(&threadPool->mutexBusy);

        // 当任务数量大于存活线程数且存活线程数小于最大线程数时添加线程
        if (queueSize > liveNum && liveNum < threadPool->maxNum) {
            addThread(threadPool);
        }

        // 当工作线程数*2小于存活线程数且存活线程数大于最小线程数时销毁线程
        if (busyNum * 2 < liveNum && liveNum > threadPool->minNum) {
            pthread_mutex_lock(&threadPool->mutexPool);
            threadPool->destroyNum = NUM_THREAD_DELETE_EVERY_TIME;
            pthread_mutex_unlock(&threadPool->mutexPool);

            // 唤醒线程让其自杀
            for (int i = 0; i < NUM_THREAD_DELETE_EVERY_TIME; i++) {
                pthread_cond_signal(&threadPool->notEmpty);
            }
        }
    }

    return NULL;
}

void addThread(ThreadPool *threadPool) {
    pthread_mutex_lock(&threadPool->mutexPool);
    for (int i = 0, cnt = 0;
         i < threadPool->maxNum && cnt < NUM_THREAD_ADD_EVERY_TIME && threadPool->liveNum < threadPool->maxNum; i++) {
        if (threadPool->threadIDs[i] == 0) {
            pthread_create(&threadPool->threadIDs[i], NULL, worker, threadPool);
            threadPool->liveNum++;
            cnt++;
        }
    }
    pthread_mutex_unlock(&threadPool->mutexPool);
}

void destroyThread(ThreadPool *threadPool) {
    pthread_t tID = pthread_self();
    for (int i = 0; i < threadPool->maxNum; i++) {
        if (threadPool->threadIDs[i] == tID) {
            threadPool->threadIDs[i] = 0;
            printf("thread %ld destroy successfully\n", tID);
            break;
        }
    }

    pthread_exit(NULL);
}

void addTask(ThreadPool *threadPool, void(*func)(void *), void *arg) {
    pthread_mutex_lock(&threadPool->mutexPool);
    if (threadPool->shutDown) {
        pthread_mutex_unlock(&threadPool->mutexPool); // 防止死锁
        return;
    }

    while (threadPool->queueSize == threadPool->queueCapacity) {
        // 阻塞生产者线程
        pthread_cond_wait(&threadPool->notFull, &threadPool->mutexPool);
    }

    // 添加任务
    threadPool->taskQueue[threadPool->queueRear].function = func;
    threadPool->taskQueue[threadPool->queueRear].arg = arg;
    threadPool->queueRear = (threadPool->queueRear + 1) % threadPool->queueCapacity;
    threadPool->queueSize++;
    pthread_cond_signal(&threadPool->notEmpty);
    pthread_mutex_unlock(&threadPool->mutexPool);
}

int getBusyNum(ThreadPool *threadPool) {
    pthread_mutex_lock(&threadPool->mutexBusy);
    int busyNum = threadPool->busyNum;
    pthread_mutex_unlock(&threadPool->mutexBusy);

    return busyNum;
}

int getLiveNum(ThreadPool *threadPool) {
    pthread_mutex_lock(&threadPool->mutexPool);
    int liveNum = threadPool->liveNum;
    pthread_mutex_unlock(&threadPool->mutexPool);

    return liveNum;
}

int destroyThreadPool(ThreadPool *threadPool) {
    if (threadPool == NULL) {
        return -1;
    }

    // 关闭线程池
    pthread_mutex_lock(&threadPool->mutexPool);
    threadPool->shutDown = 1;
    pthread_mutex_unlock(&threadPool->mutexPool);

    // 回收管理者线程
    pthread_join(threadPool->managerID, NULL);

    // 唤醒阻塞的消费者线程，让其自杀
    for (int i = 0; i < threadPool->liveNum; i++) {
        pthread_cond_signal(&threadPool->notEmpty);
    }

    // 释放堆内存
    if (threadPool->taskQueue) {
        free(threadPool->taskQueue);
    }
    if (threadPool->threadIDs) {
        free(threadPool->threadIDs);
    }
    pthread_mutex_destroy(&threadPool->mutexPool);
    pthread_mutex_destroy(&threadPool->mutexBusy);
    pthread_cond_destroy(&threadPool->notEmpty);
    pthread_cond_destroy(&threadPool->notFull);
    free(threadPool);
    threadPool = NULL;

    return 1;
}