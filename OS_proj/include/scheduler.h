#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "pcb.h"
#include "queue.h"
#include "sim_types.h"

#define MLFQ_LEVELS 4

typedef struct {
    Queue readyQueue;
    Queue blockedQueue;

    Queue mlfq[MLFQ_LEVELS];

    int quantum;
    int currentTime;
    SimMode policy;
} Scheduler;

void initScheduler(Scheduler *scheduler, int quantum, SimMode policy);

void addToReady(Scheduler *scheduler, PCB *pcb);
void addToBlocked(Scheduler *scheduler, PCB *pcb);
void unblockProcess(Scheduler *scheduler);

PCB *scheduleHRRN(Scheduler *scheduler);

PCB *scheduleRR(Scheduler *scheduler);
void runRR(Scheduler *scheduler);

void addToMLFQ(Scheduler *scheduler, PCB *pcb);
PCB *scheduleMLFQ_dequeue(Scheduler *scheduler, int *out_level);

void runMLFQ(Scheduler *scheduler);

void updateWaitingTimes(Scheduler *scheduler);
void update_waiting_times_for_mode(Scheduler *scheduler, SimMode mode);

int scheduler_has_runnable(Scheduler *scheduler, SimMode mode);

void printQueues(Scheduler *scheduler);

#endif
