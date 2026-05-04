#include <stdio.h>
#include <string.h>
#include "../include/trace.h"
#include "../include/memory.h"
#include "../include/mutex.h"
#include "../include/queue.h"
#include "../include/scheduler.h"

static int s_disk_banner_printed;

void trace_disk_format_banner_once(void) {
    if (s_disk_banner_printed)
        return;
    s_disk_banner_printed = 1;
    printf("\n"
           "[DISK FORMAT] Each swap file is named swap_<PID>.txt\n"
           "  Line 1: integer N = total words saved (4 PCB + 3 var slots + code)\n"
           "  Next N lines: one string per RAM word (plain text)\n"
           "  Empty RAM slots are stored as an empty line.\n\n");
}

void trace_swap_out(int pid, int numWords, const char *path) {
    trace_disk_format_banner_once();
    printf("[SWAP OUT] PID=%d  ->  disk file \"%s\"  (%d words)\n", pid, path, numWords);
}

void trace_swap_in(int pid, int numWords, int memStart, const char *path) {
    trace_disk_format_banner_once();
    printf("[SWAP IN]  PID=%d  <-  disk file \"%s\"  (%d words into RAM frames %d..%d)\n", pid, path,
           numWords, memStart, memStart + numWords - 1);
}

static const char *mode_str(SimMode m) {
    switch (m) {
    case SIM_RR:
        return "Round Robin";
    case SIM_HRRN:
        return "HRRN";
    case SIM_MLFQ:
        return "MLFQ";
    default:
        return "?";
    }
}

static const char *state_str(ProcessState st) {
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

void trace_scheduling_event_dispatch(PCB *p, SimMode mode, int mlfq_level, int slice_quantum, int base_quantum) {
    printf("\n");
    printf("+------------------------------ SCHEDULING EVENT ------------------------------+\n");
    printf("| DISPATCH: PID %-3d  policy=%-10s", p->pid, mode_str(mode));
    if (mode == SIM_MLFQ)
        printf("  MLFQ_level=%d  slice_Q=%d (2^level)", mlfq_level, slice_quantum);
    else if (mode == SIM_HRRN)
        printf("  non-preemptive run (until block/finish)");
    else
        printf("  time_slice=%d instr/dispatch", base_quantum);
    printf("\n");
    printf("| arrival@t=%-3d  waiting=%-3d  remaining=%-3d  burst=%-3d\n", p->arrivalTime, p->waitingTime,
           p->remainingTime, p->burstTime);
    printf("+------------------------------------------------------------------------------+\n");
}

void trace_instruction_line(PCB *p, const char *instr) {
    printf("| CPU : PID %-3d  PC=%-2d  EXEC : %s\n", p->pid, p->pc, instr ? instr : "(none)");
}

void trace_event_blocked(PCB *p, const char *reason) {
    printf("+------------------------------------------------------------------------------+\n");
    printf("| BLOCKED: PID %-3d  (%s)\n", p->pid, reason ? reason : "mutex / I/O");
    printf("+------------------------------------------------------------------------------+\n");
}

void trace_event_finished(PCB *p) {
    printf("+------------------------------------------------------------------------------+\n");
    printf("| FINISHED: PID %-3d\n", p->pid);
    printf("+------------------------------------------------------------------------------+\n");
}

void trace_event_time_jump(int from_t, int to_t) {
    printf("\n[TIME JUMP] No runnable processes: advancing clock %d -> %d (next arrival)\n\n", from_t, to_t);
}

void trace_event_arrival(PCB *p, int global_time) {
    printf("[ARRIVAL] PID=%d entered ready queue at global t=%d (configured arrival t=%d)\n", p->pid,
           global_time, p->arrivalTime);
}

void trace_queues_at_event(Scheduler *s) {
    printf("+------------------ Queues (scheduling event) ------------------+\n");
    fputs("| READY: ", stdout);
    queue_fprint_pids(stdout, &s->readyQueue);
    printf("\n");
    for (int lv = 0; lv < MLFQ_LEVELS; lv++) {
        printf("| MLFQ L%d: ", lv);
        queue_fprint_pids(stdout, &s->mlfq[lv]);
        printf("\n");
    }
    fputs("| BLOCKED (sched): ", stdout);
    queue_fprint_pids(stdout, &s->blockedQueue);
    printf("\n");
    fputs("| Mutex wait: ", stdout);
    mutex_fprint_wait_queues(stdout);
    printf("\n");
    printf("+-------------------------------------------------------------+\n\n");
}

static void print_mem_line(int w) {
    int ow = memory_get_owner(w);
    const char *tx = memory_get_word_text(w);
    char buf[72];
    strncpy(buf, tx, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    size_t L = strlen(buf);
    if (L > 52) {
        buf[49] = '.';
        buf[50] = '.';
        buf[51] = '.';
        buf[52] = '\0';
    }
    char own[12];
    if (ow < 0)
        strcpy(own, "--");
    else
        snprintf(own, sizeof(own), "%d", ow);
    printf("|  %2d  owner=%-3s  %s\n", w, own, buf);
}

void trace_after_clock_tick(Scheduler *s, PCB *procs, int nproc, SimMode mode, PCB *running) {
    (void)mode;
    int t = s->currentTime;
    for (int i = 0; i < nproc; i++) {
        if (procs[i].start >= 0)
            memory_sync_ram_metadata(&procs[i]);
    }
    printf("\n");
    printf("+--------------------------- CLOCK t = %-3d ------------------------------------+\n", t);

    printf("| Queues (after this tick)\n");
    fputs("|   READY (RR/HRRN): ", stdout);
    queue_fprint_pids(stdout, &s->readyQueue);
    printf("\n");
    for (int lv = 0; lv < MLFQ_LEVELS; lv++) {
        printf("|   MLFQ L%d: ", lv);
        queue_fprint_pids(stdout, &s->mlfq[lv]);
        printf("\n");
    }
    fputs("|   BLOCKED (scheduler list): ", stdout);
    queue_fprint_pids(stdout, &s->blockedQueue);
    printf("\n");
    fputs("|   Mutex wait: ", stdout);
    mutex_fprint_wait_queues(stdout);
    printf("\n");

    printf("| Process table\n");
    for (int i = 0; i < nproc; i++) {
        PCB *p = &procs[i];
        printf("|   PID %-3d  state=%-12s  arr@%-3d  wait=%-3d  rem=%-3d  PC=%-2d  MLFQ_lv=%d\n", p->pid,
               state_str(p->state), p->arrivalTime, p->waitingTime, p->remainingTime, p->pc, p->priorityLevel);
    }

    if (running && getState(running) == RUNNING)
        printf("| Currently executing: PID %d\n", running->pid);
    else if (running && getState(running) == BLOCKED)
        printf("| CPU released: PID %d is BLOCKED (see mutex wait queues)\n", running->pid);
    else
        printf("| Currently executing: (none)\n");

    printf("| RAM (40 words)\n");
    for (int w = 0; w < MEMORY_SIZE; w++)
        print_mem_line(w);
    printf("+------------------------------------------------------------------------------+\n\n");
}

void trace_slice_end_requeue(PCB *p, SimMode mode, int demoted_level) {
    printf("[REQUEUE] PID=%d  policy=%s", p->pid, mode_str(mode));
    if (mode == SIM_MLFQ)
        printf("  -> MLFQ level %d", demoted_level);
    printf("\n");
}

void trace_event_preempted(PCB *running, int running_level, int preempting_level) {
    printf("+------------------------------------------------------------------------------+\n");
    printf("| PREEMPTED: PID %-3d  (MLFQ L%d preempted by non-empty L%d)\n",
           running->pid, running_level, preempting_level);
    printf("+------------------------------------------------------------------------------+\n");
}

void trace_sim_header(SimMode mode, int quantum, PCB *procs, int nproc) {
    printf("\n");
    printf("================================================================================\n");
    printf(" OS SIMULATOR - %s", mode_str(mode));
    if (mode == SIM_RR)
        printf("   base quantum = %d instructions per dispatch", quantum);
    else if (mode == SIM_HRRN)
        printf("   non-preemptive (q ignored)");
    else
        printf("   MLFQ slice per level = 2^level instructions");
    printf("\n");
    printf("================================================================================\n");
    for (int i = 0; i < nproc; i++) {
        printf(" Program PID %d: arrival time = %d\n", procs[i].pid, procs[i].arrivalTime);
    }
    printf("================================================================================\n\n");
}
