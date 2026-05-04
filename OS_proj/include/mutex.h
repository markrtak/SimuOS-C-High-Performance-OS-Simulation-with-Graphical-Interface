#ifndef MUTEX_H
#define MUTEX_H

#include <stdio.h>

#include "pcb.h"
#include "scheduler.h"

void initResources(void);

int semWaitResource(Scheduler *sched, const char *resourceName, PCB *pcb);

void semSignalResource(Scheduler *sched, const char *resourceName, PCB *pcb);

void mutex_fprint_wait_queues(FILE *f);

#endif
