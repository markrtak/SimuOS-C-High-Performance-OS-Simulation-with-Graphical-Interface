# OS Simulator — `src/` source files (line-by-line guide)

This document walks through every C source file under `OS_proj/src/`: what each file does, and what each line or line range is for. Line numbers refer to the current versions of those files.

**Files (dependency order):**

| File | Role |
|------|------|
| `queue.c` | FIFO queues for ready/blocked/MLFQ/mutex waits |
| `pcb.c` | Process Control Block: state, PC, times, variables |
| `mutex.c` | Three counting-style mutexes: `file`, `userInput`, `userOutput` |
| `memory.c` | 40-word RAM, owners, code fetch, swap to/from `swap_<PID>.txt` |
| `scheduler.c` | Init scheduler, RR dequeue, HRRN pick, MLFQ dequeue, waiting-time updates |
| `trace.c` | Pretty-printed simulation log (dispatch, CPU, CLOCK, RAM, swaps) |
| `interpreter.c` | Execute one instruction from RAM for the running PCB |
| `main.c` | CLI, load programs into RAM, simulation loop, dispatch slices |

Headers live in `OS_proj/include/`; constants like `MEMORY_SIZE`, `RAM_PROCESS_OVERHEAD` are in `memory.h` / `pcb.h`.

---

## `queue.c` (72 lines)

**Purpose:** Singly-linked FIFO queue used everywhere a list of `PCB *` is needed.

| Lines | Explanation |
|-------|-------------|
| 1–4 | Includes `stdio`, `stdlib`, and project headers for `Queue` and `PCB`. |
| 6–10 | `initQueue`: set `front` and `rear` to `NULL` (empty queue). |
| 12–15 | `isEmpty`: return 1 if `front == NULL`. |
| 17–29 | `enqueue`: allocate a `Node`, attach `data`, append at `rear`; if queue was empty, both `front` and `rear` point to the new node. |
| 31–45 | `dequeue`: if empty return `NULL`; else take `data` from `front`, advance `front`, free old node; if queue becomes empty, clear `rear`. |
| 47–57 | `printQueue`: debug print of node pointers (not PIDs). |
| 59–72 | `queue_fprint_pids`: print `[pid1, pid2, ...]` to a `FILE` by walking nodes and printing each PCB’s `pid` (used in trace output). |

---

## `pcb.c` (108 lines)

**Purpose:** PCB lifecycle helpers: init, state, PC, memory bounds, HRRN ratio, variable table.

| Lines | Explanation |
|-------|-------------|
| 1–3 | Includes and `pcb.h`. |
| 5–19 | `initPCB`: set PID, `NOT_ARRIVED`, `hasArrived=0`, `pc=0`, invalid RAM bounds (`start/end = -1`), arrival, `waitingTime=0`, `burstTime` and `remainingTime`, `priorityLevel=0`, `varCount=0`, `ramFootprint=0`. |
| 21–27 | `setState` / `getState`: read/write `pcb->state`. |
| 29–35 | `incrementPC` / `getPC`: program counter in **code words** (index into instruction region after overhead). |
| 37–40 | `setMemoryBounds`: set `start`/`end` word indices in global RAM (usually set indirectly via memory layer). |
| 42–44 | `incrementWaitingTime`: add one to `waitingTime` (used when a process waits). |
| 46–49 | `calculateResponseRatio`: HRRN formula \((W + S) / S\) with \(S = \max(\text{remaining},1)\). |
| 51–67 | `pcb_set_var`: if variable name exists, update value; else append new slot if under `MAX_VARS`; return 0 on success, -1 if table full. |
| 69–75 | `pcb_get_var`: linear search by name; return `NULL` if missing. |
| 77–107 | `printPCB`: human-readable dump of PCB fields (debug). |

---

## `mutex.c` (116 lines)

**Purpose:** Three named resources behave like mutexes with FIFO wait queues.

| Lines | Explanation |
|-------|-------------|
| 1–6 | Includes mutex, scheduler, queue, sim types. |
| 8 | Internal enum: `file`, `userInput`, `userOutput` map to indices 0..2. |
| 10–14 | Each `Semaphore`: `locked`, `ownerPid`, `waitQueue`. |
| 16 | Static array of three semaphores. |
| 18–26 | `parse_resource`: string name → enum; unknown name → `RES_NONE`. |
| 28–34 | `initResources`: all unlocked, `ownerPid=-1`, empty wait queues. |
| 36–58 | `semWaitResource`: if free, lock and set owner; if same PID already owner, no-op; else **block** PCB, enqueue on wait queue, return 1 (caller should not advance PC until blocked path handled — interpreter returns early). Return 0 when lock acquired without blocking. |
| 60–93 | `semSignalResource`: only owner may signal; dequeue one waiter → `READY`, enqueue on **MLFQ level** or **ready queue** depending on policy; if no waiter, unlock. |
| 95–106 | `res_name`: index → string for trace. |
| 108–115 | `mutex_fprint_wait_queues`: print `file:[...] userInput:[...] userOutput:[...]` for trace. |

---

## `memory.c` (220 lines)

**Purpose:** Simulated RAM, ownership, code line access, RAM mirror of PCB/vars for trace, swap out/in.

| Lines | Explanation |
|-------|-------------|
| 1–6 | Includes; declares `trace.h` for swap messages. |
| 8–10 | Global `memory[]`: each word is `char*` string; `memory_owner[]`: PID or -1. |
| 12–15 | `set_owners`: mark a range as owned by one PID. |
| 17–22 | `initMemory`: all slots `NULL`, owners -1. |
| 24–37 | `findFreeBlock`: scan for **contiguous** run of `size` free words; return start index or -1. |
| 39–51 | `memory_free_range`: free malloc’d strings in PCB range, clear owners, invalidate PCB bounds. |
| 53–62 | `memory_get_code_line`: index = `start + RAM_PROCESS_OVERHEAD + pc` → current instruction string. |
| 64–79 | `mem_state_str`: `ProcessState` → short string for RAM slots. |
| 81–109 | `memory_sync_ram_metadata`: write PCB summary into first 4 words, variable slots into next 3, so RAM dump matches live PCB (with safe truncation for long names/values). |
| 111–113 | `swap_path`: build `swap_<pid>.txt`. |
| 115–143 | `swapOut`: write word count + one line per word to file, free RAM, clear owners, clear PCB bounds, trace `[SWAP OUT]`. |
| 145–201 | `swapIn`: read `N`, allocate `N` words at `memStart`, fill from lines, set owners, set PCB `start`/`end`, trace `[SWAP IN]`. |
| 203–205 | `memory_register_loaded`: set owner bits after load (used with `loadProgram`). |
| 207–211 | `memory_get_owner`: owner PID or -1. |
| 213–219 | `memory_get_word_text`: text at word or `"(free)"` if empty. |

---

## `scheduler.c` (239 lines)

**Purpose:** Scheduler state and algorithms. **Note:** `runRR` / `runMLFQ` (lines 88–185) are standalone demos; the **real** simulation uses `main.c`’s `run_process_slice` + `scheduleRR` / `scheduleHRRN` / `scheduleMLFQ_dequeue`.

| Lines | Explanation |
|-------|-------------|
| 1–3 | Includes. |
| 5–16 | `initScheduler`: init ready, blocked, all MLFQ levels; store quantum, `currentTime=0`, policy. |
| 18–21 | `addToReady`: state `READY`, enqueue ready queue. |
| 23–26 | `addToBlocked`: state `BLOCKED`, enqueue blocked queue. |
| 28–33 | `unblockProcess`: move one from blocked to ready (generic helper). |
| 35–79 | `scheduleHRRN`: scan ready list, compute response ratio for each PCB, **unlink** node with maximum RR, `free` the node, return selected PCB. |
| 81–86 | `scheduleRR`: FIFO dequeue from ready queue. |
| 88–112 | `runRR`: **legacy** loop: fake execute quantum steps with `incrementPC` only (not used by main sim). |
| 114–117 | `addToMLFQ`: put PCB at level 0. |
| 119–132 | `scheduleMLFQ_dequeue`: lowest non-empty level first; set `priorityLevel` from level. |
| 134–185 | `runMLFQ`: **legacy** MLFQ runner (not used by main sim). |
| 187–196 | `updateWaitingTimes`: increment `waitingTime` for every PCB in **ready queue only** (older helper). |
| 198–216 | `update_waiting_times_for_mode`: RR/HRRN update ready queue; MLFQ updates all MLFQ levels. |
| 218–227 | `scheduler_has_runnable`: any work in MLFQ levels or ready queue. |
| 229–238 | `printQueues`: debug print. |

---

## `trace.c` (210 lines)

**Purpose:** All formatted `printf` output for the assignment-style trace.

| Lines | Explanation |
|-------|-------------|
| 1–7 | Includes. |
| 9 | Static flag: print disk banner once. |
| 11–20 | `trace_disk_format_banner_once`: explain `swap_<PID>.txt` format. |
| 22–25 | `trace_swap_out`: one-line swap-out message. |
| 27–31 | `trace_swap_in`: swap-in with frame range. |
| 33–44 | `mode_str`: policy enum → display name. |
| 46–61 | `state_str`: process state → string. |
| 63–75 | `trace_scheduling_event_dispatch`: dispatch box with policy, quantum/slice, arrival/wait/remaining/burst. |
| 77–79 | `trace_instruction_line`: `| CPU : PID … EXEC : …`. |
| 81–85 | `trace_event_blocked`. |
| 87–91 | `trace_event_finished`. |
| 93–95 | `trace_event_time_jump`: idle gap until next arrival. |
| 97–100 | `trace_event_arrival`. |
| 102–119 | `trace_queues_at_event`: ready, MLFQ levels, blocked, mutex waits. |
| 121–140 | `print_mem_line`: one RAM word with owner and truncated text. |
| 142–186 | `trace_after_clock_tick`: sync RAM metadata for resident PCBs, print `CLOCK t`, queues, process table, “currently executing”, dump all 40 words. |
| 188–193 | `trace_slice_end_requeue`: `[REQUEUE]` line. |
| 195–209 | `trace_sim_header`: banner with policy, quantum text, per-program arrivals. |

---

## `interpreter.c` (174 lines)

**Purpose:** Execute **one** instruction for the running process: fetch from RAM via `memory_get_code_line`, mutate PCB/stdio/files/mutexes.

| Lines | Explanation |
|-------|-------------|
| 1–8 | Includes (`interpreter.h`, `memory`, `mutex`, `pcb`). |
| 10–21 | `trim`: strip leading/trailing whitespace in place. |
| 23–28 | `executeInstruction`: get code line; if missing, mark `FINISHED`. |
| 30–35 | Copy line into buffer, trim; empty line → no-op return. |
| 37–46 | `semWait …`: parse resource name; `semWaitResource` — if returns 1, **blocked** (return without `incrementPC` — actually semWait path increments PC only on success in the code: lines 42–44 increment if wait returns 0). |
| 48–56 | `semSignal …`: parse name, call `semSignalResource`, always `incrementPC`. |
| 58–100 | `assign …`: three forms: (1) `assign v readFile x` → open file, read first line into variable; (2) `assign v input` → `scanf` one token, warnings to stdout if stdin exhausted; (3) `assign v value` → store literal token. |
| 102–117 | `writeFile a b`: resolve filename and value from variables, write one line to file. |
| 119–138 | Standalone `readFile x`: read file, **printf** content (does not store in variable unless combined with assign). |
| 140–159 | `printFromTo v1 v2`: atoi of variable values, print inclusive range. |
| 161–169 | `print v`: print `Process pid: var = value`. |
| 171–173 | Unknown instruction: stderr message, still `incrementPC` to avoid infinite loop. |

---

## `main.c` (498 lines)

**Purpose:** Entry point, load program files, build PCBs in RAM, run the **clock-driven** simulation loop.

| Lines | Explanation |
|-------|-------------|
| 1–15 | Includes and `MAX_PROCESSES`. |
| 17 | Global `scheduler` instance. |
| 19–30 | `trim_inplace`: same idea as interpreter trim (for parsing file lines and arrival spec). |
| 32–76 | `load_file_lines`: read program file, skip empty/`#` lines, allocate array of instruction strings. |
| 78–82 | `free_lines`: free that array. |
| 84–128 | `loadProgram`: compute `total = instructions + RAM_PROCESS_OVERHEAD`; `findFreeBlock` or **swap out** `evictCandidate`; malloc each word; copy instructions after overhead; register owners; set burst/remaining/pc; `memory_sync_ram_metadata`. |
| 130–142 | `pick_swap_victim`: first other process with resident memory, not finished — used when forcing swap for swap-in. |
| 145–171 | `ensure_process_resident`: if PCB swapped out (`start < 0`), loop: `findFreeBlock` + `swapIn`, or `swapOut` a victim and retry. |
| 173–191 | `try_arrivals`: at/after arrival time, mark arrived, `READY`, enqueue ready or MLFQ 0, trace arrival. |
| 193–204 | `next_arrival_time`: smallest future arrival among not-yet-arrived, non-finished processes. |
| 206–212 | `all_finished`: all PCBs `FINISHED`. |
| 214–220 | `pick_next`: MLFQ dequeue, or HRRN, or RR. |
| 222–306 | `run_process_slice`: compute slice length (MLFQ: `2^level`, else `base_quantum`); `ensure_process_resident` or requeue on failure; `RUNNING`; loop up to slice: trace instruction, `executeInstruction`; if `BLOCKED`, trace block + clock tick, return; if `FINISHED`, free RAM, clock tick, return; else decrement `remainingTime`, advance time, update waits, arrivals, clock tick; after slice: requeue RR or demote MLFQ, trace requeue + queues. |
| 308–314 | `bootstrap_sim`: `initMemory`, `initScheduler`, `initResources`, `initPCB` for each process. |
| 316–358 | `simulation_loop`: until all finished — try arrivals; if nothing runnable, time-jump or deadlock message; else `pick_next`, trace dispatch + queues, `run_process_slice`; print `Simulation ended.` |
| 360–372 | `parse_arrivals`: comma-separated integers for `--arrive`. |
| 374–415 | `main_with_files`: load each file’s lines, `bootstrap_sim`, `trace_sim_header`, `loadProgram` for each (evict previous if needed), `simulation_loop`, free lines. |
| 417–443 | `main_default_demo`: two hardcoded programs, no CLI files. |
| 445–497 | `main`: parse `--quantum`, `--rr`/`--hrrn`/`--mlfq`, `--arrive`, `--help`, collect program paths; if no paths run demo else `main_with_files`. |

---

## How the pieces connect

1. **`main`** loads instructions into **`memory`** with PCB/var/code layout.
2. Each **clock tick** in the slice loop calls **`executeInstruction`**, which uses **`memory_get_code_line`**, **`pcb_*` vars**, and **`mutex`** calls.
3. **`trace_*`** prints dispatch, CPU line, and **`trace_after_clock_tick`** after each simulated tick.
4. **`ensure_process_resident`** + **`swapOut`/`swapIn`** implement demand paging when RAM is full.
5. **`scheduler`** picks the next PCB; **`queue`** holds ordered lists; **`pcb`** holds per-process state.

For instruction semantics only, see also `docs/README_interpreter.md`.
