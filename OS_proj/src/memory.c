#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/memory.h"
#include "../include/pcb.h"
#include "../include/trace.h"

char *memory[MEMORY_SIZE];

static int memory_owner[MEMORY_SIZE];

static void set_owners(int start, int end, int pid) {
    for (int i = start; i <= end; i++)
        memory_owner[i] = pid;
}

void initMemory(void) {
    for (int i = 0; i < MEMORY_SIZE; i++) {
        memory[i] = NULL;
        memory_owner[i] = -1;
    }
}

int findFreeBlock(int size) {
    if (size <= 0 || size > MEMORY_SIZE)
        return -1;
    int run = 0;
    for (int i = 0; i < MEMORY_SIZE; i++) {
        if (memory[i] == NULL && memory_owner[i] < 0)
            run++;
        else
            run = 0;
        if (run == size)
            return i - size + 1;
    }
    return -1;
}

void memory_free_range(PCB *pcb) {
    if (pcb->start < 0 || pcb->end < pcb->start)
        return;
    for (int i = pcb->start; i <= pcb->end; i++) {
        if (i >= 0 && i < MEMORY_SIZE) {
            free(memory[i]);
            memory[i] = NULL;
            memory_owner[i] = -1;
        }
    }
    pcb->start = -1;
    pcb->end = -1;
}

const char *memory_get_code_line(const PCB *pcb) {
    if (pcb->start < 0 || pcb->end < pcb->start)
        return NULL;
    int idx = pcb->start + RAM_PROCESS_OVERHEAD + pcb->pc;
    if (idx < pcb->start || idx > pcb->end || idx >= MEMORY_SIZE)
        return NULL;
    if (memory[idx] == NULL)
        return NULL;
    return memory[idx];
}

static const char *mem_state_str(ProcessState st) {
    switch (st) {
    case NOT_ARRIVED:
        return "NOT_ARRIVED";
    case READY:
        return "READY";
    case RUNNING:
        return "RUNNING";
    case BLOCKED:
        return "BLOCKED";
    case FINISHED:
        return "FINISHED";
    default:
        return "?";
    }
}

void memory_sync_ram_metadata(PCB *pcb) {
    if (!pcb || pcb->start < 0 || pcb->end < pcb->start)
        return;
    int b = pcb->start;
    if (memory[b + 0])
        snprintf(memory[b + 0], WORD_SIZE, "[PCB0] PID=%d", pcb->pid);
    if (memory[b + 1])
        snprintf(memory[b + 1], WORD_SIZE, "[PCB1] PC=%d STATE=%s", pcb->pc, mem_state_str(pcb->state));
    if (memory[b + 2])
        snprintf(memory[b + 2], WORD_SIZE, "[PCB2] wait=%d rem=%d", pcb->waitingTime, pcb->remainingTime);
    if (memory[b + 3])
        snprintf(memory[b + 3], WORD_SIZE, "[PCB3] burst=%d arr@=%d MLFQ=%d", pcb->burstTime, pcb->arrivalTime,
                 pcb->priorityLevel);
    for (int k = 0; k < RAM_VAR_SLOTS; k++) {
        if (!memory[b + RAM_PCB_WORDS + k])
            continue;
        if (k < pcb->varCount) {
            char vn[24], vv[48];
            strncpy(vn, pcb->vars[k].name, sizeof(vn) - 1);
            vn[sizeof(vn) - 1] = '\0';
            strncpy(vv, pcb->vars[k].value, sizeof(vv) - 1);
            vv[sizeof(vv) - 1] = '\0';
            snprintf(memory[b + RAM_PCB_WORDS + k], WORD_SIZE, "[VAR%d] %s=%s", k, vn, vv);
        } else {
            strncpy(memory[b + RAM_PCB_WORDS + k], "[VAR] (empty slot)", WORD_SIZE - 1);
        }
        memory[b + RAM_PCB_WORDS + k][WORD_SIZE - 1] = '\0';
    }
}

static void swap_path(char *buf, size_t len, int pid) {
    snprintf(buf, len, "swap_%d.txt", pid);
}

void swapOut(PCB *pcb) {
    if (pcb->start < 0 || pcb->end < pcb->start)
        return;

    char path[64];
    swap_path(path, sizeof(path), pcb->pid);

    FILE *f = fopen(path, "w");
    if (!f)
        return;

    int n = pcb->end - pcb->start + 1;
    fprintf(f, "%d\n", n);
    for (int i = pcb->start; i <= pcb->end; i++) {
        if (memory[i] != NULL)
            fprintf(f, "%s\n", memory[i]);
        else
            fprintf(f, "\n");
        free(memory[i]);
        memory[i] = NULL;
        memory_owner[i] = -1;
    }
    fclose(f);

    trace_swap_out(pcb->pid, n, path);

    pcb->start = -1;
    pcb->end = -1;
}

int swapIn(PCB *pcb, int memStart) {
    char path[64];
    swap_path(path, sizeof(path), pcb->pid);

    FILE *f = fopen(path, "r");
    if (!f)
        return 0;

    int n = 0;
    if (fscanf(f, "%d\n", &n) != 1 || n <= 0) {
        fclose(f);
        return 0;
    }
    if (memStart < 0 || memStart + n > MEMORY_SIZE) {
        fclose(f);
        return 0;
    }
    for (int j = memStart; j < memStart + n; j++) {
        if (memory[j] != NULL) {
            fclose(f);
            return 0;
        }
    }

    char line[WORD_SIZE];
    for (int i = 0; i < n; i++) {
        if (!fgets(line, sizeof(line), f)) {
            for (int k = 0; k < i; k++) {
                free(memory[memStart + k]);
                memory[memStart + k] = NULL;
                memory_owner[memStart + k] = -1;
            }
            fclose(f);
            return 0;
        }
        line[strcspn(line, "\r\n")] = '\0';
        memory[memStart + i] = malloc(WORD_SIZE);
        if (!memory[memStart + i]) {
            for (int k = 0; k < i; k++) {
                free(memory[memStart + k]);
                memory[memStart + k] = NULL;
                memory_owner[memStart + k] = -1;
            }
            fclose(f);
            return 0;
        }
        strncpy(memory[memStart + i], line, WORD_SIZE - 1);
        memory[memStart + i][WORD_SIZE - 1] = '\0';
        memory_owner[memStart + i] = pcb->pid;
    }
    fclose(f);

    pcb->start = memStart;
    pcb->end = memStart + n - 1;
    trace_swap_in(pcb->pid, n, memStart, path);
    return 1;
}

void memory_register_loaded(PCB *pcb, int start, int end) {
    set_owners(start, end, pcb->pid);
}

int memory_get_owner(int wordIndex) {
    if (wordIndex < 0 || wordIndex >= MEMORY_SIZE)
        return -1;
    return memory_owner[wordIndex];
}

const char *memory_get_word_text(int wordIndex) {
    if (wordIndex < 0 || wordIndex >= MEMORY_SIZE)
        return "";
    if (memory[wordIndex] == NULL)
        return "(free)";
    return memory[wordIndex];
}
