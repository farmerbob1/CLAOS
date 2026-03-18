/*
 * CLAOS — Claude Assisted Operating System
 * scheduler.c — Preemptive Round-Robin Task Scheduler
 *
 * Each task has its own 8KB stack and saved ESP. On every timer tick
 * (IRQ0), we switch to the next ready task by swapping stack pointers.
 *
 * The context switch works by:
 * 1. Timer IRQ fires, pushes registers via our ISR stub
 * 2. schedule() is called, saves current ESP into the task struct
 * 3. Picks the next READY task, loads its ESP
 * 4. Returns through the ISR stub which pops the new task's registers
 *
 * This is a simple cooperative/preemptive hybrid — tasks get preempted
 * on timer ticks but can also yield voluntarily.
 */

#include "scheduler.h"
#include "heap.h"
#include "string.h"
#include "io.h"
#include "timer.h"
#include "vga.h"

/* Task table */
static struct task tasks[MAX_TASKS];
static int current_task = -1;       /* Index of the currently running task */
static int task_count = 0;
static bool scheduler_enabled = false;

/* Defined in scheduler_asm.asm */
extern void task_switch(uint32_t* old_esp, uint32_t new_esp);

void scheduler_init(void) {
    memset(tasks, 0, sizeof(tasks));

    /* Task 0 is the "kernel main" task — it's already running.
     * We don't allocate a stack for it; it uses the existing kernel stack. */
    tasks[0].state = TASK_RUNNING;
    tasks[0].id = 0;
    tasks[0].name = "kernel";
    tasks[0].esp = 0;           /* Will be saved on first context switch */
    tasks[0].stack_base = 0;    /* Using the boot stack */

    current_task = 0;
    task_count = 1;
    scheduler_enabled = true;

    serial_print("[SCHED] Scheduler initialized\n");
}

/*
 * Create a new task. We allocate a stack and set up a fake stack frame
 * that looks like the task was interrupted mid-execution, so the context
 * switch can "resume" it naturally.
 */
int task_create(const char* name, void (*entry)(void)) {
    /* Find a free slot */
    int slot = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return -1;  /* No free slots */

    /* Allocate a stack */
    uint32_t stack = (uint32_t)kmalloc(TASK_STACK_SIZE);
    if (!stack) return -1;      /* Out of memory */

    memset((void*)stack, 0, TASK_STACK_SIZE);

    /* Set up the top of the stack (it grows downward).
     * We create a fake stack frame that task_switch will pop:
     *   - Return address (EIP) = entry function
     *   - Saved registers (EBX, ESI, EDI, EBP) = 0
     */
    uint32_t* sp = (uint32_t*)(stack + TASK_STACK_SIZE);

    /* Push a fake return address for if the task function returns.
     * Tasks shouldn't return, but if they do, we'll handle it. */
    *(--sp) = 0;               /* Fake return address (task_exit placeholder) */

    /* Push the entry point — this is where task_switch will "return" to */
    *(--sp) = (uint32_t)entry; /* EIP — task_switch does a RET to this */

    /* Push saved callee-save registers (task_switch pops these) */
    *(--sp) = 0;               /* EBP */
    *(--sp) = 0;               /* EDI */
    *(--sp) = 0;               /* ESI */
    *(--sp) = 0;               /* EBX */

    tasks[slot].esp = (uint32_t)sp;
    tasks[slot].stack_base = stack;
    tasks[slot].state = TASK_READY;
    tasks[slot].id = slot;
    tasks[slot].name = name;
    tasks[slot].sleep_until = 0;

    task_count++;
    return slot;
}

/*
 * Called from the timer IRQ handler. Picks the next task and switches.
 */
void schedule(void) {
    if (!scheduler_enabled || task_count <= 1)
        return;

    /* Wake up any sleeping tasks whose timer has expired */
    uint32_t now = timer_get_ticks();
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING && now >= tasks[i].sleep_until) {
            tasks[i].state = TASK_READY;
        }
    }

    /* Find the next ready task (round-robin) */
    int old = current_task;
    int next = current_task;

    for (int i = 0; i < MAX_TASKS; i++) {
        next = (next + 1) % MAX_TASKS;
        if (tasks[next].state == TASK_READY) {
            break;
        }
    }

    /* If no other task is ready, stay on the current one */
    if (next == old || tasks[next].state != TASK_READY)
        return;

    /* Switch! */
    if (tasks[old].state == TASK_RUNNING)
        tasks[old].state = TASK_READY;
    tasks[next].state = TASK_RUNNING;
    current_task = next;

    /* Perform the actual context switch (saves/restores registers via asm) */
    task_switch(&tasks[old].esp, tasks[next].esp);
}

struct task* task_get_current(void) {
    if (current_task >= 0)
        return &tasks[current_task];
    return NULL;
}

int task_get_count(void) {
    return task_count;
}

struct task* task_get(int index) {
    if (index >= 0 && index < MAX_TASKS && tasks[index].state != TASK_UNUSED)
        return &tasks[index];
    return NULL;
}

void task_sleep(uint32_t ms) {
    if (current_task < 0) return;

    /* Calculate the tick to wake at. Timer runs at 100 Hz = 10ms per tick. */
    tasks[current_task].sleep_until = timer_get_ticks() + (ms / 10);
    tasks[current_task].state = TASK_SLEEPING;

    /* Yield to let another task run */
    task_yield();
}

void task_yield(void) {
    schedule();
}
