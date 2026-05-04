#include <stdio.h>
#include <string.h>
#include "../include/mutex.h"
#include "../include/scheduler.h"
#include "../include/queue.h"
#include "../include/sim_types.h"

typedef enum { RES_FILE = 0, RES_USER_INPUT = 1, RES_USER_OUTPUT = 2, RES_NONE = -1 } ResourceId;

typedef struct {
    int locked;
    int ownerPid;
    Queue waitQueue;
} Semaphore;

static Semaphore resources[3];

static ResourceId parse_resource(const char *name) {
    if (strcmp(name, "file") == 0)
        return RES_FILE;
    if (strcmp(name, "userInput") == 0)
        return RES_USER_INPUT;
    if (strcmp(name, "userOutput") == 0)
        return RES_USER_OUTPUT;
    return RES_NONE;
}

void initResources(void) {
    for (int i = 0; i < 3; i++) {
        resources[i].locked = 0;
        resources[i].ownerPid = -1;
        initQueue(&resources[i].waitQueue);
    }
}

int semWaitResource(Scheduler *sched, const char *resourceName, PCB *pcb) {
    ResourceId r = parse_resource(resourceName);
    if (r == RES_NONE) {
        fprintf(stderr, "Unknown resource: %s\n", resourceName);
        return 0;
    }

    Semaphore *s = &resources[(int)r];

    if (!s->locked) {
        s->locked = 1;
        s->ownerPid = pcb->pid;
        return 0;
    }

    if (s->ownerPid == pcb->pid)
        return 0;

    setState(pcb, BLOCKED);
    enqueue(&s->waitQueue, pcb);
    (void)sched;
    return 1;
}

void semSignalResource(Scheduler *sched, const char *resourceName, PCB *pcb) {
    ResourceId rid = parse_resource(resourceName);
    if (rid == RES_NONE) {
        fprintf(stderr, "Unknown resource: %s\n", resourceName);
        return;
    }

    Semaphore *s = &resources[(int)rid];

    if (s->ownerPid != pcb->pid) {
        fprintf(stderr, "PID %d: semSignal on %s not owner (owner=%d)\n", pcb->pid, resourceName,
                s->ownerPid);
        return;
    }

    if (!isEmpty(&s->waitQueue)) {
        PCB *next = (PCB *)dequeue(&s->waitQueue);
        s->ownerPid = next->pid;
        setState(next, READY);
        if (sched->policy == SIM_MLFQ) {
            int lv = next->priorityLevel;
            if (lv < 0)
                lv = 0;
            if (lv >= MLFQ_LEVELS)
                lv = MLFQ_LEVELS - 1;
            enqueue(&sched->mlfq[lv], next);
        } else {
            enqueue(&sched->readyQueue, next);
        }
    } else {
        s->locked = 0;
        s->ownerPid = -1;
    }
}

static const char *res_name(int i) {
    switch (i) {
    case 0:
        return "file";
    case 1:
        return "userInput";
    case 2:
        return "userOutput";
    default:
        return "?";
    }
}

void mutex_fprint_wait_queues(FILE *f) {
    for (int i = 0; i < 3; i++) {
        fprintf(f, "%s:", res_name(i));
        queue_fprint_pids(f, &resources[i].waitQueue);
        if (i < 2)
            fputs("  ", f);
    }
}
