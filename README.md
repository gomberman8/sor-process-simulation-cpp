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
# defaults now come from ../config.cfg (key=value). Override with --config <path> or legacy numeric args.
./sor_sim

# or provide config explicitly
./sor_sim --config ../config.cfg
# legacy positional override still works:
./sor_sim <N_waitingRoom> <K_threshold> <simMinutes> <msPerSimMinute> <seed>

- `K_registrationThreshold=0` in the config means “auto” and sets K to N/2 to satisfy the assignment constraint (K >= N/2).
- `N_waitingRoom=50` gives more slots so blocked patient processes wake up faster in busy runs.
- `simulationDurationMinutes=0` in the config means the director lets the generator run until you stop it (SIGUSR2 / Ctrl+C).
```
Current binary prints a confirmation message; functional simulation arrives in subsequent commits.

Logger is spawned via `fork()+exec()` internally; a standalone logger mode exists for debugging:
```bash
./sor_sim logger <queueId> <logPath>
```
Registration role can be invoked directly (for debugging IPC setup):
```bash
./sor_sim registration <keyPath>   # keyPath typically the executable path used by director
```
Triage role can be invoked directly:
```bash
./sor_sim triage <keyPath>
```
Specialist role can be invoked directly (type as int from SpecialistType enum order):
```bash
./sor_sim specialist <keyPath> <typeInt>   # 0=Cardio,1=Neuro,2=Ophthalmo,3=Laryng,4=Surgeon,5=Paediatric
```
Patient generator/patient can be invoked directly:
```bash
./sor_sim patient_generator <keyPath> <N> <K> <simMinutes> <msPerMinute> <seed>
./sor_sim patient <keyPath> <id> <age> <isVip> <hasGuardian> <personsCount>
```
Director currently spawns patient generator and all specialists via exec for the demo lifecycle.
Director currently spawns one instance for each specialist type via exec.

**Note (environment):** System V IPC (msg/sem/shm) is required and may be blocked on macOS; run and test on the target Debian lab machine for correct behavior.
