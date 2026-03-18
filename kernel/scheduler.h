/*
 * CLAOS — Claude Assisted Operating System
 * scheduler.h — Preemptive Round-Robin Task Scheduler
 *
 * Supports multiple kernel tasks that are context-switched on each
 * timer tick (IRQ0). Each task has its own stack and saved register state.
 */

#ifndef CLAOS_SCHEDULER_H
#define CLAOS_SCHEDULER_H

#include "types.h"

/* Maximum number of concurrent tasks */
#define MAX_TASKS 16

/* Size of each task's kernel stack (8KB) */
#define TASK_STACK_SIZE 8192

/* Task states */
typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
} task_state_t;

/* Task control block */
struct task {
    uint32_t     esp;           /* Saved stack pointer */
    uint32_t     stack_base;    /* Base of allocated stack */
    task_state_t state;
    uint32_t     id;
    const char*  name;
    uint32_t     sleep_until;   /* Timer tick to wake at (for sleeping tasks) */
};

/* Initialize the scheduler */
void scheduler_init(void);

/* Create a new task. Returns task ID, or -1 on failure.
 * `entry` is the function the task will run — it should never return. */
int task_create(const char* name, void (*entry)(void));

/* Called from the timer IRQ to switch tasks */
void schedule(void);

/* Get the currently running task */
struct task* task_get_current(void);

/* Get the number of active tasks */
int task_get_count(void);

/* Get a task by index (0 to MAX_TASKS-1). Returns NULL if slot is unused. */
struct task* task_get(int index);

/* Sleep the current task for approximately `ms` milliseconds */
void task_sleep(uint32_t ms);

/* Yield the current task's time slice */
void task_yield(void);

#endif /* CLAOS_SCHEDULER_H */
