#include <stdio.h>
#include <string.h>
#include "../include/pcb.h"

void initPCB(PCB *pcb, int pid, int arrivalTime, int burstTime) {
    pcb->pid = pid;
    pcb->state = NOT_ARRIVED;
    pcb->hasArrived = 0;
    pcb->pc = 0;
    pcb->start = -1;
    pcb->end = -1;
    pcb->arrivalTime = arrivalTime;
    pcb->waitingTime = 0;
    pcb->burstTime = burstTime;
    pcb->remainingTime = burstTime;
    pcb->priorityLevel = 0;
    pcb->varCount = 0;
    pcb->ramFootprint = 0;
}

void setState(PCB *pcb, ProcessState state) {
    pcb->state = state;
}

ProcessState getState(PCB *pcb) {
    return pcb->state;
}

void incrementPC(PCB *pcb) {
    pcb->pc++;
}

int getPC(PCB *pcb) {
    return pcb->pc;
}

void setMemoryBounds(PCB *pcb, int start, int end) {
    pcb->start = start;
    pcb->end = end;
}

void incrementWaitingTime(PCB *pcb) {
    pcb->waitingTime++;
}

float calculateResponseRatio(PCB *pcb) {
    int s = pcb->remainingTime > 0 ? pcb->remainingTime : 1;
    return (pcb->waitingTime + s) / (float)s;
}

int pcb_set_var(PCB *pcb, const char *name, const char *value) {
    for (int i = 0; i < pcb->varCount; i++) {
        if (strcmp(pcb->vars[i].name, name) == 0) {
            strncpy(pcb->vars[i].value, value, VAR_VAL_LEN - 1);
            pcb->vars[i].value[VAR_VAL_LEN - 1] = '\0';
            return 0;
        }
    }
    if (pcb->varCount >= MAX_VARS)
        return -1;
    strncpy(pcb->vars[pcb->varCount].name, name, VAR_NAME_LEN - 1);
    pcb->vars[pcb->varCount].name[VAR_NAME_LEN - 1] = '\0';
    strncpy(pcb->vars[pcb->varCount].value, value, VAR_VAL_LEN - 1);
    pcb->vars[pcb->varCount].value[VAR_VAL_LEN - 1] = '\0';
    pcb->varCount++;
    return 0;
}

const char *pcb_get_var(const PCB *pcb, const char *name) {
    for (int i = 0; i < pcb->varCount; i++) {
        if (strcmp(pcb->vars[i].name, name) == 0)
            return pcb->vars[i].value;
    }
    return NULL;
}

void printPCB(PCB *pcb) {
    printf("\n--- PCB ---\n");
    printf("PID: %d\n", pcb->pid);
    printf("State: ");
    switch (pcb->state) {
    case NOT_ARRIVED:
        printf("NOT_ARRIVED\n");
        break;
    case READY:
        printf("READY\n");
        break;
    case RUNNING:
        printf("RUNNING\n");
        break;
    case BLOCKED:
        printf("BLOCKED\n");
        break;
    case FINISHED:
        printf("FINISHED\n");
        break;
    }
    printf("PC: %d\n", pcb->pc);
    printf("Start: %d\n", pcb->start);
    printf("End: %d\n", pcb->end);
    printf("Arrival Time: %d\n", pcb->arrivalTime);
    printf("Waiting Time: %d\n", pcb->waitingTime);
    printf("Burst Time: %d\n", pcb->burstTime);
    printf("Remaining Time: %d\n", pcb->remainingTime);
    printf("Priority Level: %d\n", pcb->priorityLevel);
    printf("-------------\n");
}
