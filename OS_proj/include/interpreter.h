#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "pcb.h"
#include "scheduler.h"

void executeInstruction(PCB *pcb, Scheduler *scheduler);

#endif
