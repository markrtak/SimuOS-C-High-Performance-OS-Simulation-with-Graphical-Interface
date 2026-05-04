#include <stdio.h>
#include <stdlib.h>
#include "../include/scheduler.h"

void initScheduler(Scheduler *scheduler, int quantum, SimMode policy) {
    initQueue(&scheduler->readyQueue);
    initQueue(&scheduler->blockedQueue);

    for (int i = 0; i < MLFQ_LEVELS; i++) {
        initQueue(&scheduler->mlfq[i]);
    }

    scheduler->quantum = quantum;
    scheduler->currentTime = 0;
    scheduler->policy = policy;
}

void addToReady(Scheduler *scheduler, PCB *pcb) {
    setState(pcb, READY);
    enqueue(&scheduler->readyQueue, pcb);
}

void addToBlocked(Scheduler *scheduler, PCB *pcb) {
    setState(pcb, BLOCKED);
    enqueue(&scheduler->blockedQueue, pcb);
}

void unblockProcess(Scheduler *scheduler) {
    if (!isEmpty(&scheduler->blockedQueue)) {
        PCB *pcb = dequeue(&scheduler->blockedQueue);
        addToReady(scheduler, pcb);
    }
}

PCB *scheduleHRRN(Scheduler *scheduler) {

    if (isEmpty(&scheduler->readyQueue))
        return NULL;

    Node *current = scheduler->readyQueue.front;
    Node *prev = NULL;

    Node *bestNode = current;
    Node *bestPrev = NULL;

    float maxRR = -1.0f;

    while (current != NULL) {

        PCB *pcb = (PCB *)current->data;

        int svc = pcb->remainingTime > 0 ? pcb->remainingTime : 1;
        float rr = (pcb->waitingTime + svc) / (float)svc;

        if (rr > maxRR) {
            maxRR = rr;
            bestNode = current;
            bestPrev = prev;
        }

        prev = current;
        current = current->next;
    }

    if (bestPrev == NULL) {
        scheduler->readyQueue.front = bestNode->next;
    } else {
        bestPrev->next = bestNode->next;
    }

    if (bestNode == scheduler->readyQueue.rear) {
        scheduler->readyQueue.rear = bestPrev;
    }

    PCB *selected = (PCB *)bestNode->data;
    free(bestNode);

    return selected;
}

PCB *scheduleRR(Scheduler *scheduler) {
    if (isEmpty(&scheduler->readyQueue))
        return NULL;

    return dequeue(&scheduler->readyQueue);
}

void runRR(Scheduler *scheduler) {

    PCB *p = scheduleRR(scheduler);
    if (!p)
        return;

    setState(p, RUNNING);

    for (int i = 0; i < scheduler->quantum; i++) {

        printf("RR: PID=%d PC=%d\n", p->pid, p->pc);

        incrementPC(p);
        p->remainingTime--;
        scheduler->currentTime++;

        if (p->remainingTime <= 0) {
            setState(p, FINISHED);
            printf("Process %d finished\n", p->pid);
            return;
        }
    }

    addToReady(scheduler, p);
}

void addToMLFQ(Scheduler *scheduler, PCB *pcb) {
    pcb->priorityLevel = 0;
    enqueue(&scheduler->mlfq[0], pcb);
}

PCB *scheduleMLFQ_dequeue(Scheduler *scheduler, int *out_level) {
    for (int i = 0; i < MLFQ_LEVELS; i++) {
        if (!isEmpty(&scheduler->mlfq[i])) {
            PCB *p = dequeue(&scheduler->mlfq[i]);
            if (out_level)
                *out_level = i;
            p->priorityLevel = i;
            return p;
        }
    }
    if (out_level)
        *out_level = -1;
    return NULL;
}

void runMLFQ(Scheduler *scheduler) {

    int level = -1;
    PCB *p = NULL;

    for (int i = 0; i < MLFQ_LEVELS; i++) {
        if (!isEmpty(&scheduler->mlfq[i])) {
            p = dequeue(&scheduler->mlfq[i]);
            level = i;
            break;
        }
    }

    if (!p)
        return;

    setState(p, RUNNING);

    int quantum = 1 << level;

    printf("MLFQ: PID=%d Level=%d Quantum=%d\n", p->pid, level, quantum);

    int usedFullQuantum = 1;

    for (int i = 0; i < quantum; i++) {

        printf("Executing PC=%d\n", p->pc);

        incrementPC(p);
        p->remainingTime--;
        scheduler->currentTime++;

        if (p->remainingTime <= 0) {
            setState(p, FINISHED);
            printf("Process %d finished\n", p->pid);
            return;
        }

        if (getState(p) == BLOCKED) {
            usedFullQuantum = 0;
            addToBlocked(scheduler, p);
            return;
        }
    }

    if (usedFullQuantum && level < MLFQ_LEVELS - 1) {
        p->priorityLevel = level + 1;
        enqueue(&scheduler->mlfq[level + 1], p);
    } else {
        enqueue(&scheduler->mlfq[level], p);
    }
}

void updateWaitingTimes(Scheduler *scheduler) {

    Node *current = scheduler->readyQueue.front;

    while (current != NULL) {
        PCB *pcb = (PCB *)current->data;
        pcb->waitingTime++;
        current = current->next;
    }
}

void update_waiting_times_for_mode(Scheduler *scheduler, SimMode mode) {
    if (mode == SIM_MLFQ) {
        for (int lv = 0; lv < MLFQ_LEVELS; lv++) {
            Node *cur = scheduler->mlfq[lv].front;
            while (cur != NULL) {
                PCB *pcb = (PCB *)cur->data;
                pcb->waitingTime++;
                cur = cur->next;
            }
        }
    } else {
        Node *cur = scheduler->readyQueue.front;
        while (cur != NULL) {
            PCB *pcb = (PCB *)cur->data;
            pcb->waitingTime++;
            cur = cur->next;
        }
    }
}

int scheduler_has_runnable(Scheduler *scheduler, SimMode mode) {
    if (mode == SIM_MLFQ) {
        for (int i = 0; i < MLFQ_LEVELS; i++) {
            if (!isEmpty(&scheduler->mlfq[i]))
                return 1;
        }
        return 0;
    }
    return !isEmpty(&scheduler->readyQueue);
}

void printQueues(Scheduler *scheduler) {

    printf("\n--- Ready Queue ---\n");
    printQueue(&scheduler->readyQueue);

    for (int i = 0; i < MLFQ_LEVELS; i++) {
        printf("\n--- MLFQ Level %d ---\n", i);
        printQueue(&scheduler->mlfq[i]);
    }
}
