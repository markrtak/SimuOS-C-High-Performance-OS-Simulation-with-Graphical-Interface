#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "../include/pcb.h"
#include "../include/scheduler.h"
#include "../include/memory.h"
#include "../include/interpreter.h"
#include "../include/mutex.h"
#include "../include/trace.h"
#include "../include/sim_types.h"

#define MAX_PROCESSES 8

Scheduler scheduler;

static void trim_inplace(char *s) {
    char *p = s;
    while (isspace((unsigned char)*p))
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
}

static int load_file_lines(const char *path, char ***out_lines, int *out_n) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return -1;
    }
    int cap = 16, n = 0;
    char **lines = malloc((size_t)cap * sizeof(char *));
    if (!lines) {
        fclose(f);
        return -1;
    }
    char buf[WORD_SIZE];
    while (fgets(buf, sizeof(buf), f)) {
        buf[strcspn(buf, "\r\n")] = '\0';
        trim_inplace(buf);
        if (buf[0] == '\0' || buf[0] == '#')
            continue;
        if (n >= cap) {
            cap *= 2;
            char **nl = realloc(lines, (size_t)cap * sizeof(char *));
            if (!nl) {
                for (int i = 0; i < n; i++)
                    free(lines[i]);
                free(lines);
                fclose(f);
                return -1;
            }
            lines = nl;
        }
        lines[n] = strdup(buf);
        if (!lines[n]) {
            for (int i = 0; i < n; i++)
                free(lines[i]);
            free(lines);
            fclose(f);
            return -1;
        }
        n++;
    }
    fclose(f);
    *out_lines = lines;
    *out_n = n;
    return 0;
}

static void free_lines(char **lines, int n) {
    for (int i = 0; i < n; i++)
        free(lines[i]);
    free(lines);
}

static int loadProgram(PCB *pcb, PCB *evictCandidate, char *instructions[], int numInstructions) {
    int total = numInstructions + RAM_PROCESS_OVERHEAD;
    pcb->ramFootprint = total;

    int memStart = findFreeBlock(total);
    if (memStart < 0 && evictCandidate && evictCandidate->start >= 0 &&
        evictCandidate->pid != pcb->pid) {
        swapOut(evictCandidate);
        memStart = findFreeBlock(total);
    }
    if (memStart < 0) {
        fprintf(stderr, "loadProgram: no %d contiguous words for pid %d (%d code + %d PCB/var overhead)\n", total,
                pcb->pid, numInstructions, RAM_PROCESS_OVERHEAD);
        return -1;
    }

    pcb->start = memStart;
    pcb->end = memStart + total - 1;

    for (int i = 0; i < total; i++) {
        memory[memStart + i] = malloc(WORD_SIZE);
        if (!memory[memStart + i]) {
            for (int j = 0; j < i; j++) {
                free(memory[memStart + j]);
                memory[memStart + j] = NULL;
            }
            pcb->start = -1;
            pcb->end = -1;
            return -1;
        }
        memory[memStart + i][0] = '\0';
    }

    for (int i = 0; i < numInstructions; i++) {
        strncpy(memory[memStart + RAM_PROCESS_OVERHEAD + i], instructions[i], WORD_SIZE - 1);
        memory[memStart + RAM_PROCESS_OVERHEAD + i][WORD_SIZE - 1] = '\0';
    }

    memory_register_loaded(pcb, pcb->start, pcb->end);
    pcb->burstTime = numInstructions;
    pcb->remainingTime = numInstructions;
    pcb->pc = 0;
    memory_sync_ram_metadata(pcb);
    return 0;
}

static PCB *pick_swap_victim(PCB *self, PCB *procs, int nproc) {
    for (int i = 0; i < nproc; i++) {
        PCB *q = &procs[i];
        if (q == self)
            continue;
        if (q->start < 0)
            continue;
        if (getState(q) == FINISHED)
            continue;
        return q;
    }
    return NULL;
}

/** Bring process image back from disk if it was swapped out (start == -1). */
static int ensure_process_resident(PCB *p, PCB *procs, int nproc) {
    if (p->start >= 0)
        return 0;
    if (getState(p) == FINISHED)
        return 0;
    if (p->ramFootprint <= 0)
        return -1;

    for (;;) {
        int memStart = findFreeBlock(p->ramFootprint);
        if (memStart >= 0) {
            if (!swapIn(p, memStart)) {
                fprintf(stderr, "PID %d: swapIn failed (missing swap_%d.txt?)\n", p->pid, p->pid);
                return -1;
            }
            memory_register_loaded(p, p->start, p->end);
            memory_sync_ram_metadata(p);
            return 0;
        }
        PCB *vic = pick_swap_victim(p, procs, nproc);
        if (!vic) {
            fprintf(stderr, "PID %d: need %d words but no swap victim available\n", p->pid, p->ramFootprint);
            return -1;
        }
        swapOut(vic);
    }
}

static void try_arrivals(Scheduler *s, PCB *procs, int n, SimMode mode) {
    for (int i = 0; i < n; i++) {
        PCB *p = &procs[i];
        if (p->hasArrived)
            continue;
        if (getState(p) == FINISHED)
            continue;
        if (s->currentTime < p->arrivalTime)
            continue;
        p->hasArrived = 1;
        setState(p, READY);
        p->priorityLevel = 0;
        trace_event_arrival(p, s->currentTime);
        if (mode == SIM_MLFQ)
            enqueue(&s->mlfq[0], p);
        else
            enqueue(&s->readyQueue, p);
    }
}

static int next_arrival_time(PCB *procs, int n, int cur_t) {
    int best = INT_MAX;
    for (int i = 0; i < n; i++) {
        if (procs[i].hasArrived)
            continue;
        if (getState(&procs[i]) == FINISHED)
            continue;
        if (procs[i].arrivalTime > cur_t && procs[i].arrivalTime < best)
            best = procs[i].arrivalTime;
    }
    return best == INT_MAX ? -1 : best;
}

static int all_finished(PCB *procs, int n) {
    for (int i = 0; i < n; i++) {
        if (getState(&procs[i]) != FINISHED)
            return 0;
    }
    return 1;
}

static PCB *pick_next(Scheduler *s, SimMode mode, int *mlfq_lv) {
    if (mode == SIM_MLFQ)
        return scheduleMLFQ_dequeue(s, mlfq_lv);
    if (mode == SIM_HRRN)
        return scheduleHRRN(s);
    return scheduleRR(s);
}

static int mlfq_has_higher_priority(Scheduler *s, int current_level) {
    for (int lv = 0; lv < current_level; lv++) {
        if (!isEmpty(&s->mlfq[lv]))
            return lv;
    }
    return -1;
}

static void run_process_slice(PCB *p, Scheduler *s, SimMode mode, int mlfq_level, int base_quantum,
                              PCB *procs, int nproc) {
    int slice_q = 1;
    if (mode == SIM_MLFQ)
        slice_q = (1 << mlfq_level);
    else if (mode == SIM_RR)
        slice_q = base_quantum;
    else
        slice_q = p->remainingTime;
    if (slice_q < 1)
        slice_q = 1;

    if (ensure_process_resident(p, procs, nproc) != 0) {
        fprintf(stderr, "PID %d: cannot run — not memory-resident and swap-in failed\n", p->pid);
        setState(p, READY);
        if (mode == SIM_MLFQ) {
            int lv = p->priorityLevel;
            if (lv < 0 || lv >= MLFQ_LEVELS)
                lv = 0;
            enqueue(&s->mlfq[lv], p);
        } else
            enqueue(&s->readyQueue, p);
        s->currentTime++;
        update_waiting_times_for_mode(s, mode);
        try_arrivals(s, procs, nproc, mode);
        trace_after_clock_tick(s, procs, nproc, mode, NULL);
        return;
    }

    setState(p, RUNNING);

    int steps_done = 0;

    for (int step = 0; step < slice_q; step++) {
        if (p->remainingTime <= 0)
            break;

        const char *instr = memory_get_code_line(p);
        trace_instruction_line(p, instr);

        executeInstruction(p, s);

        if (getState(p) == BLOCKED) {
            trace_event_blocked(p, "mutex wait");
            return;
        }

        if (getState(p) == FINISHED) {
            printf("Process %d FINISHED\n", p->pid);
            trace_event_finished(p);
            memory_free_range(p);
            trace_after_clock_tick(s, procs, nproc, mode, NULL);
            return;
        }

        p->remainingTime--;
        s->currentTime++;
        steps_done++;
        update_waiting_times_for_mode(s, mode);
        try_arrivals(s, procs, nproc, mode);
        trace_after_clock_tick(s, procs, nproc, mode, p);

        if (mode == SIM_MLFQ && mlfq_level > 0) {
            int higher = mlfq_has_higher_priority(s, mlfq_level);
            if (higher >= 0) {
                trace_event_preempted(p, mlfq_level, higher);
                setState(p, READY);
                enqueue(&s->mlfq[mlfq_level], p);
                trace_slice_end_requeue(p, mode, mlfq_level);
                trace_queues_at_event(s);
                return;
            }
        }
    }

    if (getState(p) == FINISHED)
        return;

    if (p->remainingTime <= 0) {
        setState(p, FINISHED);
        printf("Process %d FINISHED\n", p->pid);
        trace_event_finished(p);
        memory_free_range(p);
        trace_after_clock_tick(s, procs, nproc, mode, NULL);
        return;
    }

    setState(p, READY);
    if (mode == SIM_MLFQ) {
        int newlev = mlfq_level;
        if (steps_done == slice_q && mlfq_level < MLFQ_LEVELS - 1) {
            newlev = mlfq_level + 1;
            p->priorityLevel = newlev;
        }
        enqueue(&s->mlfq[newlev], p);
        trace_slice_end_requeue(p, mode, newlev);
    } else {
        enqueue(&s->readyQueue, p);
        trace_slice_end_requeue(p, mode, -1);
    }
    trace_queues_at_event(s);
}

static void bootstrap_sim(PCB *procs, int nproc, const int *arrivals, int quantum, SimMode mode) {
    initMemory();
    initScheduler(&scheduler, quantum, mode);
    initResources();
    for (int i = 0; i < nproc; i++)
        initPCB(&procs[i], i + 1, arrivals[i], 0);
}

static void simulation_loop(PCB *procs, int nproc, SimMode mode, int base_quantum) {
    Scheduler *s = &scheduler;

    try_arrivals(s, procs, nproc, mode);
    trace_queues_at_event(s);

    while (!all_finished(procs, nproc)) {
        try_arrivals(s, procs, nproc, mode);

        if (!scheduler_has_runnable(s, mode)) {
            if (all_finished(procs, nproc))
                break;
            int nxt = next_arrival_time(procs, nproc, s->currentTime);
            if (nxt < 0) {
                printf("No runnable processes and no future arrivals (possible deadlock).\n");
                trace_queues_at_event(s);
                break;
            }
            trace_event_time_jump(s->currentTime, nxt);
            s->currentTime = nxt;
            try_arrivals(s, procs, nproc, mode);
            continue;
        }

        int mlfq_lv = -1;
        PCB *p = pick_next(s, mode, &mlfq_lv);
        if (!p)
            break;
        if (getState(p) == FINISHED)
            continue;

        int slice_q = 1;
        if (mode == SIM_MLFQ)
            slice_q = (1 << mlfq_lv);
        else if (mode == SIM_RR)
            slice_q = base_quantum;
        else
            slice_q = p->remainingTime; // HRRN dispatch length in trace.
        if (slice_q < 1)
            slice_q = 1;

        trace_scheduling_event_dispatch(p, mode, mlfq_lv, slice_q, base_quantum);
        trace_queues_at_event(s);

        run_process_slice(p, s, mode, mlfq_lv, base_quantum, procs, nproc);
    }

    printf("\nSimulation ended.\n");
}

static int parse_arrivals(const char *spec, int *out, int maxn) {
    int n = 0;
    char buf[256];
    strncpy(buf, spec, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *tok = strtok(buf, ",");
    while (tok && n < maxn) {
        trim_inplace(tok);
        out[n++] = atoi(tok);
        tok = strtok(NULL, ",");
    }
    return n;
}

static int main_with_files(char *paths[], int npath, int quantum, SimMode mode, int *arrivals, int nar_spec) {
    char **line_arrays[MAX_PROCESSES];
    int line_counts[MAX_PROCESSES];

    int arr[MAX_PROCESSES];
    for (int i = 0; i < npath; i++) {
        if (i < nar_spec)
            arr[i] = arrivals[i];
        else
            arr[i] = 0;
    }

    for (int i = 0; i < npath; i++) {
        if (load_file_lines(paths[i], &line_arrays[i], &line_counts[i]) != 0)
            return 1;
        if (line_counts[i] <= 0) {
            fprintf(stderr, "No instructions in %s\n", paths[i]);
            for (int j = 0; j <= i; j++)
                free_lines(line_arrays[j], line_counts[j]);
            return 1;
        }
    }

    PCB procs[MAX_PROCESSES];
    bootstrap_sim(procs, npath, arr, quantum, mode);
    trace_sim_header(mode, quantum, procs, npath);

    for (int i = 0; i < npath; i++) {
        PCB *evict = (i > 0) ? &procs[i - 1] : NULL;
        if (loadProgram(&procs[i], evict, line_arrays[i], line_counts[i]) != 0) {
            for (int j = 0; j < npath; j++)
                free_lines(line_arrays[j], line_counts[j]);
            return 1;
        }
    }

    simulation_loop(procs, npath, mode, quantum);

    for (int i = 0; i < npath; i++)
        free_lines(line_arrays[i], line_counts[i]);
    return 0;
}

static int main_default_demo(void) {
    int arr[2] = {0, 0};
    PCB procs[2];
    bootstrap_sim(procs, 2, arr, 2, SIM_RR);
    trace_sim_header(SIM_RR, 2, procs, 2);

    char *prog1[] = {
        "semWait userInput",
        "assign x input",
        "semSignal userInput",
        "print x"
    };
    char *prog2[] = {
        "semWait userInput",
        "assign y input",
        "semSignal userInput",
        "print y"
    };

    if (loadProgram(&procs[0], NULL, prog1, 4) != 0)
        return 1;
    if (loadProgram(&procs[1], &procs[0], prog2, 4) != 0)
        return 1;

    simulation_loop(procs, 2, SIM_RR, 2);
    return 0;
}

int main(int argc, char **argv) {
    int quantum = 2;
    SimMode mode = SIM_RR;
    char *paths[MAX_PROCESSES];
    int npath = 0;
    int arr_spec[MAX_PROCESSES];
    int nar_spec = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quantum") == 0 && i + 1 < argc) {
            quantum = atoi(argv[++i]);
            if (quantum < 1)
                quantum = 1;
            continue;
        }
        if (strcmp(argv[i], "--hrrn") == 0) {
            mode = SIM_HRRN;
            continue;
        }
        if (strcmp(argv[i], "--rr") == 0) {
            mode = SIM_RR;
            continue;
        }
        if (strcmp(argv[i], "--mlfq") == 0) {
            mode = SIM_MLFQ;
            continue;
        }
        if (strcmp(argv[i], "--arrive") == 0 && i + 1 < argc) {
            nar_spec = parse_arrivals(argv[++i], arr_spec, MAX_PROCESSES);
            continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            fprintf(stderr,
                    "Usage: %s [options] <Program_1.txt> [Program_2.txt ...]\n"
                    "  --quantum N     instructions per slice (RR only)\n"
                    "  --rr | --hrrn | --mlfq\n"
                    "  --arrive t0,t1,...  per-process arrival times (default 0)\n"
                    "  (no files: built-in 2-process demo, RR q=2)\n",
                    argv[0]);
            return 0;
        }
        if (npath < MAX_PROCESSES)
            paths[npath++] = argv[i];
        else {
            fprintf(stderr, "Too many programs (max %d)\n", MAX_PROCESSES);
            return 1;
        }
    }

    if (npath == 0)
        return main_default_demo();
    return main_with_files(paths, npath, quantum, mode, arr_spec, nar_spec);
}
