# SOR Process Simulation (Work in Progress)

University Operating Systems project: Szpitalny Oddział Ratunkowy (SOR) simulation using C++17 and POSIX/System V IPC on Linux (Debian x86 lab target). No third-party libs.

## Assignment Highlights
- Multi-process design with `fork()` + `exec()`; no threads.
- Event-driven via System V message queues (registration, triage, specialists, logging).
- Shared memory for global counters/state, semaphores for waiting room capacity and state protection.
- Dedicated logger process writing via low-level I/O.
- Signals: `SIGUSR1` (specialist temporarily leaves), `SIGUSR2` (immediate evacuation/cleanup).
- Robust input validation and error handling after every syscall; IPC cleanup with `IPC_RMID`.

## Project Layout
- `sor-simulation/` — source, include, build artifacts.
  - `src/` and `include/` mirror: director, roles (patient generator, patient, registration, triage, specialist), IPC wrappers, logging, utils.
- `docs/api.md` — contracts and examples for all custom functions/classes.

## Build and Run (current stub)
```bash
cd sor-simulation
mkdir -p build
cd build
cmake ..
cmake --build .
./sor_sim
```
Current binary prints a confirmation message; functional simulation arrives in subsequent commits.
