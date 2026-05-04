#ifndef MEMORY_H
#define MEMORY_H

#include "pcb.h"

#define MEMORY_SIZE 40
#define WORD_SIZE 100

/** Per-process RAM layout (project spec): 4 words for PCB summary, 3 for variable slots, then code. */
#define RAM_PCB_WORDS 4
#define RAM_VAR_SLOTS 3
#define RAM_PROCESS_OVERHEAD (RAM_PCB_WORDS + RAM_VAR_SLOTS)

extern char *memory[MEMORY_SIZE];

void initMemory(void);

int findFreeBlock(int size);

void memory_free_range(PCB *pcb);

const char *memory_get_code_line(const PCB *pcb);

void swapOut(PCB *pcb);

int swapIn(PCB *pcb, int memStart);

void memory_register_loaded(PCB *pcb, int start, int end);

int memory_get_owner(int wordIndex);

const char *memory_get_word_text(int wordIndex);

/** Refresh PCB + var slot lines in RAM from live PCB (no-op if not resident). */
void memory_sync_ram_metadata(PCB *pcb);

#endif
