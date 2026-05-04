#ifndef PCB_H
#define PCB_H

#define MAX_VARS 16
#define VAR_NAME_LEN 32
#define VAR_VAL_LEN 100

typedef struct {
    char name[VAR_NAME_LEN];
    char value[VAR_VAL_LEN];
} VarSlot;

typedef enum {
    NOT_ARRIVED,
    READY,
    RUNNING,
    BLOCKED,
    FINISHED
} ProcessState;

typedef struct PCB {
    int pid;
    ProcessState state;

    int pc;
    int start;
    int end;

    int arrivalTime;
    int waitingTime;
    int burstTime;
    int remainingTime;

    int priorityLevel;

    VarSlot vars[MAX_VARS];
    int varCount;

    /** Total words this process occupies in RAM (PCB slots + var slots + code). Used for swap. */
    int ramFootprint;

    int hasArrived;
} PCB;

void initPCB(PCB *pcb, int pid, int arrivalTime, int burstTime);

void setState(PCB *pcb, ProcessState state);
ProcessState getState(PCB *pcb);

void incrementPC(PCB *pcb);
int getPC(PCB *pcb);

void setMemoryBounds(PCB *pcb, int start, int end);

void incrementWaitingTime(PCB *pcb);
float calculateResponseRatio(PCB *pcb);

int pcb_set_var(PCB *pcb, const char *name, const char *value);
const char *pcb_get_var(const PCB *pcb, const char *name);

void printPCB(PCB *pcb);

#endif
