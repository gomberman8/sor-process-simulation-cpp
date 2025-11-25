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
# defaults: N=10 K=5 duration=60 totalPatients=20 msPerMinute=100 seed=12345
./sor_sim

# or provide config explicitly
./sor_sim <N_waitingRoom> <K_threshold> <simMinutes> <totalPatients> <msPerSimMinute> <seed>
```
Current binary prints a confirmation message; functional simulation arrives in subsequent commits.

Logger is spawned via `fork()+exec()` internally; a standalone logger mode exists for debugging:
```bash
./sor_sim logger <queueId> <logPath>
```

**Note (environment):** System V IPC (msg/sem/shm) is required and may be blocked on macOS; run and test on the target Debian lab machine for correct behavior.
