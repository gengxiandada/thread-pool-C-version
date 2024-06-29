//
// Created by 程思浩 on 2024/6/29.
//

#ifndef THREAD_POOL_THREADPOOL_H
#define THREAD_POOL_THREADPOOL_H

typedef struct ThreadPool ThreadPool;

// 创建线程池并初始化
ThreadPool *createThreadPool(int min, int max, int queueSize);

void *worker(void *arg);

void *manager(void *arg);

// 添加任务
void addTask(ThreadPool *threadPool, void(*func)(void *), void *arg);

// 获取线程池中工作线程个数
int getBusyNum(ThreadPool *threadPool);

// 获取线程池中活跃线程个数
int getLiveNum(ThreadPool *threadPool);

// 添加线程
void addThread(ThreadPool *threadPool);

// 销毁线程
void destroyThread(ThreadPool *threadPool);

// 销毁线程池
int destroyThreadPool(ThreadPool *threadPool);

#endif //THREAD_POOL_THREADPOOL_H
