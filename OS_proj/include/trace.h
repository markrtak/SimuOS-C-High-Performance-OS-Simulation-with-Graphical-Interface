#ifndef TRACE_H
#define TRACE_H

#include "scheduler.h"
#include "sim_types.h"
#include "pcb.h"

#include <stdio.h>

void trace_disk_format_banner_once(void);

void trace_swap_out(int pid, int numWords, const char *path);
void trace_swap_in(int pid, int numWords, int memStart, const char *path);

void trace_scheduling_event_dispatch(PCB *p, SimMode mode, int mlfq_level, int slice_quantum, int base_quantum);

void trace_instruction_line(PCB *p, const char *instr);

void trace_event_blocked(PCB *p, const char *reason);

void trace_event_finished(PCB *p);

void trace_event_time_jump(int from_t, int to_t);

void trace_event_arrival(PCB *p, int global_time);

void trace_queues_at_event(Scheduler *s);

void trace_after_clock_tick(Scheduler *s, PCB *procs, int nproc, SimMode mode, PCB *running);

void trace_slice_end_requeue(PCB *p, SimMode mode, int demoted_level);

void trace_event_preempted(PCB *running, int running_level, int preempting_level);

void trace_sim_header(SimMode mode, int quantum, PCB *procs, int nproc);

#endif
