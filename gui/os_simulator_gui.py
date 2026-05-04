"""
OS Simulator — CSEN 602 visual timeline console
Run from repo root:  python gui/os_simulator_gui.py
Requires: pip install -r gui/requirements.txt
Build simulator first:  cmake --build build   (produces build/os_sim.exe)
"""

from __future__ import annotations

import re
import subprocess
import sys
import threading
from dataclasses import dataclass, field
from pathlib import Path
from tkinter import END

try:
    import customtkinter as ctk
except ImportError:
    print("Install CustomTkinter:  pip install -r gui/requirements.txt", file=sys.stderr)
    sys.exit(1)

ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = ROOT / "build"
PROGRAM_DIR = ROOT / "OS_proj" / "programs"
PROGRAM_FILES = ("Program_1.txt", "Program_2.txt", "Program_3.txt")

C = {
    "bg":         "#000000",
    "surface":    "#121212",
    "card":       "#1e1e1e",
    "border":     "#3a3a3a",
    "accent":     "#38bdf8",
    "accent2":    "#60a5fa",
    "accent_dim": "#1d4ed8",
    "success":    "#9a9a9a",
    "warn":       "#94a3b8",
    "err":        "#bfdbfe",
    "text":       "#e6e6e6",
    "muted":      "#9c9c9c",
    "dark":       "#0a0a0a",
    "p1":         "#7dd3fc",
    "p2":         "#93c5fd",
    "p3":         "#a0a0a0",
    "p4":         "#888888",
    "free_ram":   "#2a2a2a",
}

PID_COLORS = {1: C["p1"], 2: C["p2"], 3: C["p3"], 4: C["p4"]}
STATE_COLORS = {
    "RUNNING":     C["accent"],
    "READY":       C["success"],
    "BLOCKED":     C["warn"],
    "FINISHED":    C["muted"],
    "NOT_ARRIVED": "#64748b",
}


def find_executable() -> Path | None:
    for name in ("os_sim.exe", "os_sim"):
        p = BUILD_DIR / name
        if p.is_file():
            return p
    return None


# ── Snapshot parser ──────────────────────────────────────────────────────────

@dataclass
class ProcessSnap:
    pid: int = 0
    state: str = ""
    arrival: int = 0
    wait: int = 0
    remaining: int = 0
    pc: int = 0
    mlfq: int = 0


@dataclass
class RamWord:
    owner: int = -1
    text: str = "(free)"


@dataclass
class ClockSnap:
    t: int = 0
    events: list[str] = field(default_factory=list)
    ready: str = "[]"
    mlfq: list[str] = field(default_factory=lambda: ["[]"] * 4)
    blocked: str = "[]"
    mutex_wait: str = ""
    procs: list[ProcessSnap] = field(default_factory=list)
    executing: str = "(none)"
    cpu_line: str = ""
    ram: list[RamWord] = field(default_factory=lambda: [RamWord() for _ in range(40)])


_RE_CLOCK      = re.compile(r"CLOCK t = (\d+)")
_RE_READY      = re.compile(r"READY \(RR/HRRN\):\s*(\[.*?\])")
_RE_MLFQ       = re.compile(r"MLFQ L(\d):\s*(\[.*?\])")
_RE_BLOCKED    = re.compile(r"BLOCKED \(scheduler list\):\s*(\[.*?\])")
_RE_MUTEX      = re.compile(r"Mutex wait:\s*(.*)")
_RE_PROC       = re.compile(
    r"PID\s+(\d+)\s+state=(\S+)\s+arr@(\d+)\s+wait=(\d+)\s+rem=(\d+)\s+PC=(\d+)\s+MLFQ_lv=(\d+)"
)
_RE_EXEC       = re.compile(r"Currently executing:\s*(.*)")
_RE_CPU_RELEASE = re.compile(r"CPU released:\s*(.*)")
_RE_RAM        = re.compile(r"\|\s+(\d+)\s+owner=(\S+)\s+(.*)")
_RE_CPU        = re.compile(r"\|\s*CPU\s*:\s*(.*)")
_RE_SWAP       = re.compile(r"\[SWAP\s+(IN|OUT)\].*")
_RE_DISPATCH   = re.compile(r"\|\s*DISPATCH:.*")
_RE_ARRIVAL    = re.compile(r"\[ARRIVAL\].*")
_RE_REQUEUE    = re.compile(r"\[REQUEUE\].*")
_RE_FINISHED   = re.compile(r"\|\s*FINISHED:.*")
_RE_BLOCKED_EV = re.compile(r"\|\s*BLOCKED:.*PID.*")
_RE_TIME_JUMP  = re.compile(r"\[TIME JUMP\].*")
_RE_PROG_OUT   = re.compile(r"Process \d+:.*")
_RE_INPUT_PROMPT = re.compile(r"\(PID \d+\) enter value.*")
_RE_INPUT_WARN = re.compile(r"\[INPUT\].*")
_RE_DISK_FMT   = re.compile(r"\[DISK FORMAT\].*")

_EVENT_PATS = (_RE_SWAP, _RE_DISPATCH, _RE_ARRIVAL, _RE_REQUEUE,
               _RE_FINISHED, _RE_BLOCKED_EV, _RE_TIME_JUMP,
               _RE_PROG_OUT, _RE_INPUT_PROMPT, _RE_INPUT_WARN,
               _RE_DISK_FMT)


def _parse_raw_blocks(raw: str) -> list[ClockSnap]:
    """First pass: one ClockSnap per CLOCK block (may have duplicate t values)."""
    snaps: list[ClockSnap] = []
    current: ClockSnap | None = None
    pending_events: list[str] = []
    pending_cpu: str = ""
    in_clock_block = False

    for line in raw.splitlines():
        stripped = line.rstrip()

        m = _RE_CPU.search(stripped)
        if m:
            pending_cpu = m.group(1).strip()

        for pat in _EVENT_PATS:
            if pat.search(stripped):
                ev = stripped.strip().lstrip("|").strip().lstrip("+").strip()
                pending_events.append(ev)
                break

        mc = _RE_CLOCK.search(stripped)
        if mc:
            t = int(mc.group(1))
            current = ClockSnap(t=t)
            current.events = list(pending_events)
            current.cpu_line = pending_cpu
            snaps.append(current)
            pending_events.clear()
            pending_cpu = ""
            in_clock_block = True
            continue

        if not current or not in_clock_block:
            continue

        if stripped.startswith("+---") and current.procs:
            in_clock_block = False
            continue

        m = _RE_READY.search(stripped)
        if m:
            current.ready = m.group(1)
        m = _RE_MLFQ.search(stripped)
        if m:
            idx = int(m.group(1))
            if 0 <= idx < 4:
                current.mlfq[idx] = m.group(2)
        m = _RE_BLOCKED.search(stripped)
        if m:
            current.blocked = m.group(1)
        m = _RE_MUTEX.search(stripped)
        if m:
            current.mutex_wait = m.group(1).strip()
        m = _RE_PROC.search(stripped)
        if m:
            current.procs.append(ProcessSnap(
                pid=int(m.group(1)), state=m.group(2), arrival=int(m.group(3)),
                wait=int(m.group(4)), remaining=int(m.group(5)),
                pc=int(m.group(6)), mlfq=int(m.group(7)),
            ))
        m = _RE_EXEC.search(stripped)
        if m:
            current.executing = m.group(1).strip()
        m = _RE_CPU_RELEASE.search(stripped)
        if m:
            current.executing = m.group(1).strip()
        m = _RE_RAM.search(stripped)
        if m:
            idx = int(m.group(1))
            ow_s = m.group(2)
            ow = -1 if ow_s == "--" else int(ow_s)
            if 0 <= idx < 40:
                current.ram[idx] = RamWord(owner=ow, text=m.group(3).strip())

    if pending_events and snaps:
        snaps[-1].events.extend(pending_events)

    return snaps


def parse_output(raw: str) -> list[ClockSnap]:
    """Parse simulator output into one snapshot per unique clock tick.

    When multiple CLOCK blocks share the same t value (e.g. a process
    finishes and the next one is dispatched in the same tick), they are
    merged: events are concatenated, and state/RAM come from the last block.
    """
    raw_blocks = _parse_raw_blocks(raw)
    if not raw_blocks:
        return []

    merged: list[ClockSnap] = []
    for blk in raw_blocks:
        if merged and merged[-1].t == blk.t:
            prev = merged[-1]
            prev.events.extend(blk.events)
            if blk.cpu_line:
                prev.cpu_line = blk.cpu_line
            prev.ready = blk.ready
            prev.mlfq = blk.mlfq
            prev.blocked = blk.blocked
            if blk.mutex_wait:
                prev.mutex_wait = blk.mutex_wait
            prev.procs = blk.procs
            prev.executing = blk.executing
            prev.ram = blk.ram
        else:
            merged.append(blk)

    return merged


# ── GUI ──────────────────────────────────────────────────────────────────────

FONT = "Consolas"
FONT_UI = "Segoe UI"


class App(ctk.CTk):
    def __init__(self) -> None:
        super().__init__()
        self.title("OS-602 Simulator — Timeline View")
        self.geometry("1500x920")
        self.minsize(1100, 700)
        self.configure(fg_color=C["bg"])
        ctk.set_appearance_mode("dark")
        ctk.set_default_color_theme("dark-blue")

        self._snaps: list[ClockSnap] = []
        self._cur = 0
        self._raw_out = ""
        self._auto_id: str | None = None
        self._tick_btns: list[ctk.CTkButton] = []

        self._build()
        self._load_programs()

        self.bind("<Left>", lambda e: self._go(-1))
        self.bind("<Right>", lambda e: self._go(1))
        self.bind("<Home>", lambda e: self._jump(0))
        self.bind("<End>", lambda e: self._jump(len(self._snaps) - 1) if self._snaps else None)

    # ── layout ───────────────────────────────────────────────────────────

    def _build(self) -> None:
        self.grid_columnconfigure(0, weight=0, minsize=320)
        self.grid_columnconfigure(1, weight=1)
        self.grid_rowconfigure(1, weight=1)

        self._build_header()
        self._build_left()
        self._build_right()

    def _build_header(self) -> None:
        h = ctk.CTkFrame(self, fg_color="transparent", height=44)
        h.grid(row=0, column=0, columnspan=2, sticky="ew", padx=20, pady=(12, 2))
        h.grid_columnconfigure(2, weight=1)

        ctk.CTkLabel(h, text=" CSEN 602 ",
                     font=ctk.CTkFont(family=FONT_UI, size=11, weight="bold"),
                     text_color=C["bg"], fg_color=C["accent"], corner_radius=4
                     ).grid(row=0, column=0, padx=(0, 8))
        ctk.CTkLabel(h, text="OS Simulator",
                     font=ctk.CTkFont(family=FONT_UI, size=18, weight="bold"),
                     text_color=C["text"]).grid(row=0, column=1, sticky="w")
        self._status = ctk.CTkLabel(h, text="Ready",
                                    font=ctk.CTkFont(family=FONT, size=11),
                                    text_color=C["muted"])
        self._status.grid(row=0, column=3, padx=12)

    # ── left sidebar ─────────────────────────────────────────────────────

    def _build_left(self) -> None:
        left = ctk.CTkFrame(self, fg_color=C["surface"], corner_radius=10, width=320)
        left.grid(row=1, column=0, sticky="nsew", padx=(16, 6), pady=(2, 12))
        left.grid_rowconfigure(4, weight=1)
        left.grid_columnconfigure(0, weight=1)

        # scheduler
        ctl = ctk.CTkFrame(left, fg_color="transparent")
        ctl.grid(row=0, column=0, sticky="ew", padx=10, pady=(10, 2))
        ctl.grid_columnconfigure(1, weight=1)

        ctk.CTkLabel(ctl, text="Quantum", font=ctk.CTkFont(size=11),
                     text_color=C["muted"]).grid(row=0, column=0, sticky="w")
        self._quantum = ctk.CTkSlider(ctl, from_=1, to=8, number_of_steps=7, width=120,
                                      button_color=C["accent"])
        self._quantum.set(2)
        self._quantum.grid(row=0, column=1, padx=4)
        self._qval = ctk.CTkLabel(ctl, text="2", font=ctk.CTkFont(family=FONT, size=12),
                                  text_color=C["accent"])
        self._qval.grid(row=0, column=2)
        self._quantum.configure(command=lambda v: self._qval.configure(text=str(int(round(v)))))

        self._sched = ctk.CTkSegmentedButton(ctl, values=["Round Robin", "HRRN", "MLFQ"],
                                             font=ctk.CTkFont(size=10, weight="bold"),
                                             selected_color=C["accent_dim"],
                                             unselected_color=C["border"])
        self._sched.set("Round Robin")
        self._sched.grid(row=1, column=0, columnspan=3, sticky="ew", pady=(4, 0))

        # arrivals + stdin
        pm = ctk.CTkFrame(left, fg_color="transparent")
        pm.grid(row=1, column=0, sticky="ew", padx=10, pady=(6, 0))
        pm.grid_columnconfigure(0, weight=1)

        ctk.CTkLabel(pm, text="Arrivals (comma-sep)",
                     font=ctk.CTkFont(size=10), text_color=C["muted"]
                     ).grid(row=0, column=0, sticky="w")
        self._arrive = ctk.CTkEntry(pm, font=ctk.CTkFont(family=FONT, size=11),
                                    fg_color=C["dark"], border_color=C["border"], height=26)
        self._arrive.grid(row=1, column=0, sticky="ew")
        self._arrive.insert(0, "0,1,4")

        ctk.CTkLabel(pm, text="Input prompts appear at runtime",
                     font=ctk.CTkFont(size=10, slant="italic"),
                     text_color=C["muted"]
                     ).grid(row=2, column=0, sticky="w", pady=(4, 0))
        ctk.CTkLabel(pm,
                     text="Each 'assign X input' opens a dialog.\nUse Skip to insert a sensible default.",
                     font=ctk.CTkFont(size=9), text_color=C["muted"],
                     justify="left"
                     ).grid(row=3, column=0, sticky="w")

        # buttons row
        br = ctk.CTkFrame(left, fg_color="transparent")
        br.grid(row=2, column=0, sticky="ew", padx=10, pady=(8, 2))
        br.grid_columnconfigure(0, weight=1)

        self._run_btn = ctk.CTkButton(
            br, text="Run Simulation",
            font=ctk.CTkFont(family=FONT_UI, size=14, weight="bold"),
            height=38, corner_radius=8, fg_color=C["accent_dim"],
            hover_color=C["accent"], text_color=C["bg"], command=self._on_run)
        self._run_btn.grid(row=0, column=0, sticky="ew")

        self._raw_btn = ctk.CTkButton(
            br, text="Show Raw Output",
            font=ctk.CTkFont(size=11), height=28, corner_radius=6,
            fg_color=C["border"], hover_color=C["accent_dim"],
            command=self._show_raw_output)
        self._raw_btn.grid(row=1, column=0, sticky="ew", pady=(4, 0))

        # program file label
        ctk.CTkLabel(left, text="Program Files",
                     font=ctk.CTkFont(size=11, weight="bold"),
                     text_color=C["muted"]
                     ).grid(row=3, column=0, sticky="w", padx=12, pady=(6, 0))

        # program tabs
        self._tabs = ctk.CTkTabview(left, fg_color=C["card"],
                                    segmented_button_fg_color=C["border"],
                                    segmented_button_selected_color=C["accent_dim"],
                                    text_color=C["text"], height=200)
        self._tabs.grid(row=4, column=0, sticky="nsew", padx=6, pady=(0, 6))
        self._editors: dict[str, ctk.CTkTextbox] = {}
        for fn in PROGRAM_FILES:
            nm = fn.replace(".txt", "")
            self._tabs.add(nm)
            tab = self._tabs.tab(nm)
            tab.grid_rowconfigure(0, weight=1)
            tab.grid_columnconfigure(0, weight=1)
            tb = ctk.CTkTextbox(tab, font=ctk.CTkFont(family=FONT, size=11),
                                fg_color=C["dark"], text_color=C["success"],
                                border_color=C["border"], border_width=1, wrap="none")
            tb.grid(row=0, column=0, sticky="nsew", padx=2, pady=2)
            self._editors[fn] = tb

    # ── right: timeline + panels ─────────────────────────────────────────

    def _build_right(self) -> None:
        right = ctk.CTkFrame(self, fg_color="transparent")
        right.grid(row=1, column=1, sticky="nsew", padx=(6, 16), pady=(2, 12))
        right.grid_rowconfigure(2, weight=1)
        right.grid_columnconfigure(0, weight=1)

        # ─ timeline bar (row 0) ─
        tbar = ctk.CTkFrame(right, fg_color=C["surface"], corner_radius=10, height=48)
        tbar.grid(row=0, column=0, sticky="ew", pady=(0, 4))
        tbar.grid_columnconfigure(4, weight=1)

        self._prev_btn = ctk.CTkButton(tbar, text="<", width=36, height=30,
                                       font=ctk.CTkFont(size=14, weight="bold"),
                                       fg_color=C["border"], hover_color=C["accent_dim"],
                                       command=lambda: self._go(-1))
        self._prev_btn.grid(row=0, column=0, padx=(10, 2), pady=8)

        self._next_btn = ctk.CTkButton(tbar, text=">", width=36, height=30,
                                       font=ctk.CTkFont(size=14, weight="bold"),
                                       fg_color=C["border"], hover_color=C["accent_dim"],
                                       command=lambda: self._go(1))
        self._next_btn.grid(row=0, column=1, padx=2, pady=8)

        self._play_btn = ctk.CTkButton(tbar, text="Play", width=50, height=30,
                                       font=ctk.CTkFont(size=11, weight="bold"),
                                       fg_color=C["accent_dim"], hover_color=C["accent"],
                                       text_color=C["bg"], command=self._toggle_play)
        self._play_btn.grid(row=0, column=2, padx=(6, 2), pady=8)

        self._tick_label = ctk.CTkLabel(tbar, text="t = --",
                                        font=ctk.CTkFont(family=FONT, size=15, weight="bold"),
                                        text_color=C["accent"])
        self._tick_label.grid(row=0, column=3, padx=(8, 4))

        self._slider = ctk.CTkSlider(tbar, from_=0, to=1, number_of_steps=1, height=16,
                                     button_color=C["accent"], progress_color=C["accent_dim"],
                                     fg_color=C["border"])
        self._slider.set(0)
        self._slider.grid(row=0, column=4, sticky="ew", padx=(4, 12), pady=8)
        self._slider.configure(command=self._on_slider)

        # ─ tick buttons row (row 1) ─
        self._tick_scroll = ctk.CTkScrollableFrame(right, fg_color=C["surface"],
                                                   corner_radius=8, height=36,
                                                   orientation="horizontal")
        self._tick_scroll.grid(row=1, column=0, sticky="ew", pady=(0, 4))

        # ─ main panels (row 2) ─
        panels = ctk.CTkFrame(right, fg_color="transparent")
        panels.grid(row=2, column=0, sticky="nsew")
        panels.grid_columnconfigure(0, weight=2)
        panels.grid_columnconfigure(1, weight=3)
        panels.grid_rowconfigure(0, weight=1)

        # -- left info column --
        info = ctk.CTkFrame(panels, fg_color="transparent")
        info.grid(row=0, column=0, sticky="nsew", padx=(0, 4))
        info.grid_rowconfigure(3, weight=1)
        info.grid_columnconfigure(0, weight=1)

        # CPU
        cpu = self._make_card(info, "CPU — Executing")
        cpu.grid(row=0, column=0, sticky="ew", pady=(0, 4))
        self._cpu_lbl = ctk.CTkLabel(cpu, text="--",
                                     font=ctk.CTkFont(family=FONT, size=13, weight="bold"),
                                     text_color=C["accent"], wraplength=400, justify="left")
        self._cpu_lbl.grid(row=1, column=0, sticky="w", padx=10, pady=(0, 8))

        # Events
        ev = self._make_card(info, "Events / Swaps")
        ev.grid(row=1, column=0, sticky="ew", pady=(0, 4))
        self._events_box = ctk.CTkTextbox(ev, height=72,
                                          font=ctk.CTkFont(family=FONT, size=10),
                                          fg_color=C["dark"], text_color=C["warn"],
                                          border_width=0, wrap="word")
        self._events_box.grid(row=1, column=0, sticky="ew", padx=6, pady=(0, 6))
        self._events_box.configure(state="disabled")

        # Queues
        qc = self._make_card(info, "Queues")
        qc.grid(row=2, column=0, sticky="ew", pady=(0, 4))
        self._queues_box = ctk.CTkTextbox(qc, height=80,
                                          font=ctk.CTkFont(family=FONT, size=10),
                                          fg_color=C["dark"], text_color=C["text"],
                                          border_width=0, wrap="word")
        self._queues_box.grid(row=1, column=0, sticky="ew", padx=6, pady=(0, 6))
        self._queues_box.configure(state="disabled")

        # Process table
        pt = self._make_card(info, "Process Table")
        pt.grid(row=3, column=0, sticky="nsew")
        pt.grid_rowconfigure(1, weight=1)
        self._ptable_box = ctk.CTkTextbox(pt, font=ctk.CTkFont(family=FONT, size=10),
                                          fg_color=C["dark"], text_color=C["text"],
                                          border_width=0, wrap="none")
        self._ptable_box.grid(row=1, column=0, sticky="nsew", padx=6, pady=(0, 6))
        self._ptable_box.configure(state="disabled")

        # -- right: RAM --
        ram = self._make_card(panels, "RAM (40 words)")
        ram.grid(row=0, column=1, sticky="nsew", padx=(4, 0))
        ram.grid_rowconfigure(1, weight=1)
        self._ram_box = ctk.CTkTextbox(ram, font=ctk.CTkFont(family=FONT, size=10),
                                       fg_color=C["dark"], text_color=C["text"],
                                       border_width=0, wrap="none")
        self._ram_box.grid(row=1, column=0, sticky="nsew", padx=6, pady=(0, 6))
        self._ram_box.configure(state="disabled")

    def _make_card(self, parent: ctk.CTkFrame, title: str) -> ctk.CTkFrame:
        card = ctk.CTkFrame(parent, fg_color=C["card"], corner_radius=8)
        card.grid_columnconfigure(0, weight=1)
        ctk.CTkLabel(card, text=title,
                     font=ctk.CTkFont(size=10, weight="bold"),
                     text_color=C["muted"]
                     ).grid(row=0, column=0, sticky="w", padx=10, pady=(6, 0))
        return card

    # ── programs ─────────────────────────────────────────────────────────

    def _load_programs(self) -> None:
        PROGRAM_DIR.mkdir(parents=True, exist_ok=True)
        for fn in PROGRAM_FILES:
            p = PROGRAM_DIR / fn
            tb = self._editors[fn]
            tb.delete("1.0", END)
            tb.insert("1.0", p.read_text(encoding="utf-8") if p.is_file() else f"# {fn}\n")

    def _save_programs(self) -> None:
        PROGRAM_DIR.mkdir(parents=True, exist_ok=True)
        for fn, tb in self._editors.items():
            (PROGRAM_DIR / fn).write_text(tb.get("1.0", END).rstrip() + "\n", encoding="utf-8")

    # ── run simulation ───────────────────────────────────────────────────

    _INPUT_DEFAULTS = {
        "x": "1", "y": "5",
        "a": "output.txt", "b": "hello",
    }

    _RE_NEED_INPUT = re.compile(r"\[NEED_INPUT\]\s+pid=(\d+)\s+var=(\S+)")

    def _default_for(self, var: str) -> str:
        return self._INPUT_DEFAULTS.get(var, "0")

    def _on_run(self) -> None:
        exe = find_executable()
        if not exe:
            self._status.configure(text="No build! cmake --build build", text_color=C["err"])
            return

        self._save_programs()
        self._run_btn.configure(state="disabled")
        self._status.configure(text="Running...", text_color=C["warn"])
        self._stop_play()
        self._raw_out = ""

        q = int(round(self._quantum.get()))
        policy = self._sched.get()
        arrive_spec = self._arrive.get().strip()

        cmd = [str(exe), "--quantum", str(q)]
        if policy == "HRRN":
            cmd.append("--hrrn")
        elif policy == "MLFQ":
            cmd.append("--mlfq")
        else:
            cmd.append("--rr")
        if arrive_spec:
            cmd.extend(["--arrive", arrive_spec])
        for fn in PROGRAM_FILES:
            cmd.append(str((PROGRAM_DIR / fn).resolve()))

        try:
            proc = subprocess.Popen(
                cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, bufsize=1, text=True,
                encoding="utf-8", errors="replace", cwd=str(BUILD_DIR),
            )
        except Exception as e:
            self._status.configure(text=str(e)[:60], text_color=C["err"])
            self._run_btn.configure(state="normal")
            return

        self._proc = proc
        threading.Thread(target=self._reader_loop, args=(proc,), daemon=True).start()

    def _reader_loop(self, proc: subprocess.Popen) -> None:
        try:
            assert proc.stdout is not None
            for line in proc.stdout:
                self._raw_out += line
                if _RE_CLOCK.search(line):
                    self.after(0, self._partial_refresh)
                m = self._RE_NEED_INPUT.search(line)
                if m:
                    pid = int(m.group(1))
                    var = m.group(2)
                    done = threading.Event()
                    response: dict = {}
                    self.after(0, self._partial_refresh)
                    self.after(0, lambda: self._ask_input(pid, var, response, done))
                    done.wait()
                    val = response.get("value", self._default_for(var))
                    try:
                        if proc.stdin:
                            proc.stdin.write(val + "\n")
                            proc.stdin.flush()
                    except Exception:
                        pass
            proc.wait()
        except Exception as e:
            self.after(0, lambda: self._status.configure(text=str(e)[:60], text_color=C["err"]))
        finally:
            self.after(0, self._on_sim_done)
            self.after(0, lambda: self._run_btn.configure(state="normal"))

    def _partial_refresh(self) -> None:
        """Re-parse accumulated output and show the latest tick — called mid-run."""
        snaps = parse_output(self._raw_out)
        if not snaps:
            return
        prev_n = len(self._snaps)
        self._snaps = snaps
        n = len(snaps)
        if n != prev_n:
            self._slider.configure(to=max(n - 1, 1), number_of_steps=max(n - 1, 1))
            self._rebuild_tick_buttons()
        self._cur = n - 1
        self._slider.set(self._cur)
        self._status.configure(text=f"Running... t={snaps[-1].t}  ({n} ticks)", text_color=C["warn"])
        self._show_snap(self._cur)

    def _ask_input(self, pid: int, var: str, response: dict, done: threading.Event) -> None:
        dlg = ctk.CTkToplevel(self)
        dlg.title(f"Input needed - PID {pid}")
        dlg.geometry("380x180")
        dlg.configure(fg_color=C["bg"])
        dlg.transient(self)
        dlg.grab_set()
        dlg.protocol("WM_DELETE_WINDOW", lambda: None)

        pid_color = PID_COLORS.get(pid, C["text"])
        ctk.CTkLabel(
            dlg, text=f"PID {pid}  enter value for  '{var}'",
            font=ctk.CTkFont(family=FONT_UI, size=14, weight="bold"),
            text_color=pid_color,
        ).pack(pady=(18, 6))

        ctk.CTkLabel(
            dlg, text=f"Skip -> default: {self._default_for(var)!r}",
            font=ctk.CTkFont(size=10), text_color=C["muted"],
        ).pack(pady=(0, 8))

        entry = ctk.CTkEntry(
            dlg, font=ctk.CTkFont(family=FONT, size=13),
            fg_color=C["dark"], border_color=C["accent"], width=280, height=32,
        )
        entry.pack(pady=2)
        entry.focus_set()

        def submit() -> None:
            val = entry.get().strip()
            response["value"] = val if val else self._default_for(var)
            done.set()
            dlg.destroy()

        def skip() -> None:
            response["value"] = self._default_for(var)
            done.set()
            dlg.destroy()

        row = ctk.CTkFrame(dlg, fg_color="transparent")
        row.pack(pady=14)
        ctk.CTkButton(
            row, text="Submit", width=110, height=30,
            font=ctk.CTkFont(family=FONT_UI, size=12, weight="bold"),
            fg_color=C["accent_dim"], hover_color=C["accent"],
            text_color=C["bg"], command=submit,
        ).pack(side="left", padx=6)
        ctk.CTkButton(
            row, text="Skip (use default)", width=140, height=30,
            font=ctk.CTkFont(family=FONT_UI, size=11),
            fg_color=C["border"], hover_color=C["warn"],
            text_color=C["text"], command=skip,
        ).pack(side="left", padx=6)

        entry.bind("<Return>", lambda e: submit())
        dlg.bind("<Escape>", lambda e: skip())

    def _on_sim_done(self) -> None:
        self._snaps = parse_output(self._raw_out)
        if not self._snaps:
            self._status.configure(text="No clock ticks parsed — check Raw Output", text_color=C["err"])
            return
        n = len(self._snaps)
        self._cur = 0
        self._slider.configure(to=max(n - 1, 1), number_of_steps=max(n - 1, 1))
        self._slider.set(0)
        self._status.configure(text=f"{n} clock cycles", text_color=C["success"])
        self._rebuild_tick_buttons()
        self._show_snap(0)

    # ── tick buttons ─────────────────────────────────────────────────────

    def _rebuild_tick_buttons(self) -> None:
        for b in self._tick_btns:
            b.destroy()
        self._tick_btns.clear()

        for i, s in enumerate(self._snaps):
            btn = ctk.CTkButton(
                self._tick_scroll, text=f"t{s.t}", width=38, height=26,
                font=ctk.CTkFont(family=FONT, size=10, weight="bold"),
                fg_color=C["border"], hover_color=C["accent_dim"],
                text_color=C["text"], corner_radius=6,
                command=lambda idx=i: self._jump(idx),
            )
            btn.pack(side="left", padx=1, pady=2)
            self._tick_btns.append(btn)

    def _highlight_tick_btn(self, idx: int) -> None:
        for i, btn in enumerate(self._tick_btns):
            if i == idx:
                btn.configure(fg_color=C["accent_dim"], text_color=C["bg"])
            else:
                btn.configure(fg_color=C["border"], text_color=C["text"])

    # ── navigation ───────────────────────────────────────────────────────

    def _jump(self, idx: int) -> None:
        if not self._snaps:
            return
        idx = max(0, min(idx, len(self._snaps) - 1))
        self._cur = idx
        self._slider.set(idx)
        self._show_snap(idx)

    def _go(self, delta: int) -> None:
        if not self._snaps:
            return
        self._jump(self._cur + delta)

    def _on_slider(self, val: float) -> None:
        if not self._snaps:
            return
        idx = max(0, min(int(round(val)), len(self._snaps) - 1))
        if idx != self._cur:
            self._cur = idx
            self._show_snap(idx)

    # ── auto-play ────────────────────────────────────────────────────────

    def _toggle_play(self) -> None:
        if self._auto_id is not None:
            self._stop_play()
        else:
            self._start_play()

    def _start_play(self) -> None:
        if not self._snaps:
            return
        self._play_btn.configure(text="Pause", fg_color=C["warn"], text_color=C["bg"])
        self._auto_step()

    def _stop_play(self) -> None:
        if self._auto_id is not None:
            self.after_cancel(self._auto_id)
            self._auto_id = None
        self._play_btn.configure(text="Play", fg_color=C["accent_dim"], text_color=C["bg"])

    def _auto_step(self) -> None:
        if self._cur < len(self._snaps) - 1:
            self._go(1)
            self._auto_id = self.after(600, self._auto_step)
        else:
            self._stop_play()

    # ── display a snapshot ───────────────────────────────────────────────

    def _set_box(self, box: ctk.CTkTextbox, text: str) -> None:
        box.configure(state="normal")
        box.delete("1.0", END)
        box.insert("1.0", text)
        box.configure(state="disabled")

    def _show_snap(self, idx: int) -> None:
        s = self._snaps[idx]
        total = len(self._snaps)
        self._tick_label.configure(text=f"t = {s.t}   [{idx + 1}/{total}]")
        self._highlight_tick_btn(idx)

        # CPU
        cpu_text = s.cpu_line if s.cpu_line else s.executing
        self._cpu_lbl.configure(text=cpu_text)

        # Events — separate program output from scheduling events
        prog_output = [e for e in s.events
                       if e.startswith("Process ") or "(PID " in e or e.startswith("[INPUT]")]
        sched_events = [e for e in s.events if e not in prog_output]

        parts: list[str] = []
        if prog_output:
            parts.append(">>> " + "\n>>> ".join(prog_output))
        if sched_events:
            parts.extend(sched_events)
        ev_text = "\n".join(parts) if parts else "(none)"
        self._set_box(self._events_box, ev_text)

        # Queues
        q_lines = [f"Ready:    {s.ready}"]
        has_mlfq = any(mq != "[]" for mq in s.mlfq)
        if has_mlfq:
            for i, mq in enumerate(s.mlfq):
                q_lines.append(f"MLFQ L{i}:  {mq}")
        q_lines.append(f"Blocked:  {s.blocked}")
        if s.mutex_wait:
            q_lines.append(f"Mutex:    {s.mutex_wait}")
        self._set_box(self._queues_box, "\n".join(q_lines))

        # Process table
        hdr = f"{'PID':>4} {'State':<12} {'Arr':>4} {'Wait':>5} {'Rem':>4} {'PC':>3} {'MLF':>3}"
        sep = "-" * len(hdr)
        rows = [hdr, sep]
        for p in s.procs:
            rows.append(
                f"{p.pid:>4} {p.state:<12} {p.arrival:>4} {p.wait:>5} {p.remaining:>4} {p.pc:>3} {p.mlfq:>3}"
            )
        self._set_box(self._ptable_box, "\n".join(rows))

        # RAM
        ram_lines: list[str] = []
        for i, w in enumerate(s.ram):
            ow = "--" if w.owner < 0 else f"P{w.owner}"
            ram_lines.append(f" {i:>2}  {ow:>4}  {w.text}")
        self._set_box(self._ram_box, "\n".join(ram_lines))

    # ── raw output popup ─────────────────────────────────────────────────

    def _show_raw_output(self) -> None:
        if not self._raw_out:
            self._status.configure(text="Run simulation first", text_color=C["warn"])
            return
        win = ctk.CTkToplevel(self)
        win.title("Raw Simulator Output")
        win.geometry("900x700")
        win.configure(fg_color=C["bg"])
        win.grid_rowconfigure(0, weight=1)
        win.grid_columnconfigure(0, weight=1)
        tb = ctk.CTkTextbox(win, font=ctk.CTkFont(family=FONT, size=11),
                            fg_color=C["dark"], text_color=C["text"], wrap="none")
        tb.grid(row=0, column=0, sticky="nsew", padx=10, pady=10)
        tb.insert("1.0", self._raw_out)
        tb.configure(state="disabled")


def main() -> None:
    App().mainloop()


if __name__ == "__main__":
    main()
