#ifndef TASK_H
#define TASK_H

#include <stdatomic.h>
#include <pthread.h>
#include "base.h"

typedef void (* TaskQueueCallback)(void * data);

typedef struct {
    TaskQueueCallback callback;
    void * data;
} TaskQueueEntry;

typedef struct {
    // NOTE(traks): not modded, but allowed to wrap around
    _Atomic u32 writeCommit;
    // NOTE(traks): modded by size
    _Atomic u32 writeIndex;
    _Atomic u32 readIndex;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    TaskQueueEntry entries[256];
} TaskQueue;

void CreateTaskQueue(TaskQueue * queue, i32 threadCount);
i32 PushTaskToQueue(TaskQueue * queue, TaskQueueCallback callback, void * data);

#endif
