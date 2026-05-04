# 🖥️ SimuOS — C OS Simulator with Graphical Interface

![C](https://img.shields.io/badge/C-A8B9CC?style=for-the-badge&logo=c&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-064F8C?style=for-the-badge&logo=cmake&logoColor=white)
![Python](https://img.shields.io/badge/Python-3776AB?style=for-the-badge&logo=python&logoColor=white)
![CustomTkinter](https://img.shields.io/badge/CustomTkinter-1F6FEB?style=for-the-badge)

A **CPU–memory–scheduler simulation** written in C, bundled with a **Python desktop GUI** for live visualization. The core models a small instruction interpreter, a fixed RAM with **disk swapping**, **mutex-style resources**, and **multi-policy scheduling** (round-robin, HRRN, and MLFQ).

<a id="project-demo"></a>
## 📺 Project Demo

Full walkthrough of the simulator, GUI, and scheduling modes:

* **[Watch the full project demo]([https://drive.google.com/file/d/PLACEHOLDER_REPLACE_ME/view?usp=sharing](https://drive.google.com/file/d/1I1X9vYAHUIwugI9v8_1gS0AFgdwuR6FD/view?usp=sharing))**
* Direct link (copy/paste): `https://drive.google.com/file/d/PLACEHOLDER_REPLACE_ME/view?usp=sharing`

## 🚀 Features

* **Bytecode-style interpreter**: Processes execute text instructions loaded from program files into simulated RAM, with a program counter and per-process variables.
* **Memory management**: Contiguous allocation in a word-addressable RAM; **swap-out / swap-in** to `swap_<PID>.txt` when space is tight.
* **Advanced scheduling**: **Round-Robin** (configurable quantum), **HRRN** (non-preemptive response-ratio selection), and **MLFQ** (multi-level ready queues).
* **Synchronization**: Mutex-like resources (`file`, `userInput`, `userOutput`) with FIFO wait queues and owner tracking.
* **Trace logging**: Structured simulation trace (dispatch, CPU activity, clock, RAM state, swap events) for debugging and reports.
* **Graphical interface**: **CustomTkinter** GUI (`gui/os_simulator_gui.py`) to observe process states, memory, and scheduler behavior in real time alongside the simulator.

## 🛠️ Tech Stack

* **Core simulator**: C11
* **Build system**: CMake
* **Desktop UI**: Python 3 + **customtkinter**
* **Programs**: Plain-text instruction files under `OS_proj/programs/` (see samples `Program_1.txt` … `Program_3.txt`)

## 🔧 Setup & Installation

1. **Clone the repository**:
    ```bash
    git clone https://github.com/markrtak/SimuOS-C-High-Performance-OS-Simulation-with-Graphical-Interface.git
    cd SimuOS-C-High-Performance-OS-Simulation-with-Graphical-Interface
    ```

2. **Build the C simulator** (from the repo root, where `CMakeLists.txt` lives):
    ```bash
    cmake -S . -B build
    cmake --build build
    ```
    The executable is `build/os_sim.exe` on Windows or `build/os_sim` on Unix-like systems.

3. **Run the simulator (CLI)** (on Windows the binary is `build\os_sim.exe`; on macOS/Linux use `./build/os_sim`):
    ```bash
    build/os_sim --help
    build/os_sim OS_proj/programs/Program_1.txt OS_proj/programs/Program_2.txt --rr --quantum 2
    build/os_sim OS_proj/programs/Program_1.txt --hrrn
    build/os_sim OS_proj/programs/Program_1.txt --mlfq --arrive 0,3,5
    ```
    With **no program arguments**, the binary runs a small **built-in two-process RR demo** (quantum 2).

4. **Run the graphical interface**:
    ```bash
    pip install -r gui/requirements.txt
    python gui/os_simulator_gui.py
    ```
    On Windows you can also double-click or run `gui/run_gui.bat` from the repo (it starts the GUI from the project root).

## 🧬 System Architecture

The design separates **fast C-side simulation** from **optional Python visualization**. **CMake** produces a single `os_sim` executable that owns the scheduler, memory, mutex layer, and interpreter loop. The **GUI** is a separate process that presents queues, RAM, and process metadata in a responsive layout, mirroring the same concepts described in `OS_proj/docs/README_src.md` for the C sources.
