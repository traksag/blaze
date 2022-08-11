#include "task.h"

static TaskQueueEntry PopOrAwaitTaskFromQueue(TaskQueue * queue) {
    for (;;) {
        TaskQueueEntry res = {0};
        u32 size = ARRAY_SIZE(queue->entries);
        u32 readIndex = atomic_load_explicit(&queue->readIndex, memory_order_acquire);
        u32 writeIndex = atomic_load_explicit(&queue->writeIndex, memory_order_acquire);
        if (writeIndex == readIndex) {
            pthread_mutex_lock(&queue->mutex);
            u32 newWriteIndex = atomic_load_explicit(&queue->writeIndex, memory_order_acquire);
            if (newWriteIndex != writeIndex) {
                pthread_mutex_unlock(&queue->mutex);
            } else {
                pthread_cond_wait(&queue->cond, &queue->mutex);
                pthread_mutex_unlock(&queue->mutex);
            }
        } else {
            u32 nextReadIndex = (readIndex + 1) % size;
            res = queue->entries[readIndex];
            if (atomic_compare_exchange_strong_explicit(&queue->readIndex, &readIndex, nextReadIndex, memory_order_acq_rel, memory_order_relaxed)) {
                return res;
            }
        }
    }
}

static void * RunThread(void * arg) {
#ifdef PROFILE
    TracyCSetThreadName("Worker");
#endif

    TaskQueue * queue = arg;

    for (;;) {
        TaskQueueEntry found = PopOrAwaitTaskFromQueue(queue);
        if (found.callback != NULL) {
            found.callback(found.data);
        }
    }

    return NULL;
}

void CreateTaskQueue(TaskQueue * queue, i32 threadCount) {
    // TODO(traks): handle errors

    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);

    for (i32 threadIndex = 0; threadIndex < threadCount; threadIndex++) {
        pthread_t thread;
        pthread_create(&thread, NULL, RunThread, queue);
    }
}

i32 PushTaskToQueue(TaskQueue * queue, TaskQueueCallback callback, void * data) {
    u32 size = ARRAY_SIZE(queue->entries);
    TaskQueueEntry entry = {
        .callback = callback,
        .data = data,
    };

    for (;;) {
        u32 writeCommit = atomic_load_explicit(&queue->writeCommit, memory_order_acquire);
        u32 readIndex = atomic_load_explicit(&queue->readIndex, memory_order_acquire);
        u32 writeIndex = writeCommit % size;
        u32 nextWriteIndex = (writeCommit + 1) % size;

        if (nextWriteIndex == readIndex) {
            return 0;
        }

        // @NOTE(traks) if a bunch of reads/writes happen here, the write commit
        // theoretically be wrapped around back to our position, and then we can
        // cause trouble. Therefore we don't mod the write commit by the size.

        if (atomic_compare_exchange_strong_explicit(&queue->writeCommit, &writeCommit, writeCommit + 1, memory_order_acq_rel, memory_order_relaxed)) {
            // NOTE(traks): at this point no one can write to the index, other
            // than us. Anyone else would have to write past the reader index,
            // the but the reader index is waiting for us
            queue->entries[writeIndex] = entry;
            for (;;) {
                if (atomic_compare_exchange_weak_explicit(&queue->writeIndex, &writeIndex, nextWriteIndex, memory_order_acq_rel, memory_order_relaxed)) {
                    pthread_mutex_lock(&queue->mutex);
                    pthread_cond_signal(&queue->cond);
                    pthread_mutex_unlock(&queue->mutex);
                    return 1;
                }
            }
        }
    }
}
