# SOR Process Simulation (C++17 + System V IPC)
Multi-process Szpitalny Oddział Ratunkowy simulation using only the C++17 standard library and System V IPC (msg/sem/shm) on Linux.

## Build & Run
```bash
cd sor-simulation
mkdir -p build && cd build
cmake ..
cmake --build .

# run with default config lookup (config.cfg then ../config.cfg)
./sor_sim
# or point to a config file
./sor_sim --config ../config.cfg
# legacy positional args: ./sor_sim <N_waitingRoom> <K_threshold> <simMinutes> <msPerSimMinute> <seed>
```

Config keys (`config.cfg`):
- `N_waitingRoom`, `K_registrationThreshold` (0 => auto N/2), `simulationDurationMinutes` (<=0 = until SIGUSR2/Ctrl+C), `timeScaleMsPerSimMinute`, `randomSeed`, `visualizerRenderIntervalMs`.

## Assignment highlights
- Multi-process pipeline: `fork()` + `exec()` per role (director, logger, registration 1/2, triage, six specialists, patient generator, visualizer).
- SysV IPC mix: message queues (registration/triage/specialists/logging), shared memory for counters, semaphores for waiting-room capacity + shared-state mutex.
- Signals: `SIGUSR1` pauses a specialist; `SIGUSR2` evacuates; workers ignore `SIGINT` so the director controls shutdown.
- Robustness: input validation, per-syscall error checks (`errno`), minimal permissions (`0600`), cleanup via `IPC_RMID`/`semctl(IPC_RMID)`/`shmctl(IPC_RMID)` after each run.
- Visibility: dedicated logger writes semicolon-separated lines consumed by the TUI visualizer.

## End-to-end workflow (with permalinks)
- Director bootstraps IPC (ftok/msgget/msgctl/semget/shmget) and spawns all children via fork/exec; see [queues](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/director.cpp#L85-L156), [semaphores](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/director.cpp#L158-L193), [shared memory](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/director.cpp#L195-L221), and [process lifecycle](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/director.cpp#L486-L735).
- Logger process blocks on `msgrcv()` until `END`, writing lines with `open`/`write`/`close`: [runLogger](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/logging/logger.cpp#L130-L178).
- PatientGenerator opens IPC and repeatedly `fork()`/`execv()` patients, cleaning up with `kill()`/`waitpid()`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/roles/patient_generator.cpp#L57-L220).
- Patient acquires waiting-room semaphores, enqueues via `msgsnd()`, and honors `SIGUSR2`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/roles/patient.cpp#L65-L199).
- Registration consumes with `msgrcv()`, updates shared state under semaphore, forwards to triage via `msgsnd()`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/roles/registration.cpp#L46-L160).
- Triage reads patients, optionally sends home (posting semaphores), or routes to specialist queues via `msgsnd()`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/roles/triage.cpp#L77-L237).
- Specialists handle `SIGUSR1`/`SIGUSR2`, consume prioritized patients with `msgrcv()`, update shared outcomes: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/roles/specialist.cpp#L75-L211).
- Visualizer tails the log file; when launched by Director it uses the configured refresh interval (no IPC).

## IPC reference (system calls and wrappers)
- Message queues (`msgget`/`msgsnd`/`msgrcv`/`msgctl`):
  - [create](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/ipc/message_queue.cpp#L14-L22)
  - [send](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/ipc/message_queue.cpp#L24-L45)
  - [receive](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/ipc/message_queue.cpp#L47-L64)
  - [destroy](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/ipc/message_queue.cpp#L70-L80)
  - [open](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/ipc/message_queue.cpp#L83-L90)
- Semaphores (`semget`/`semop`/`semctl`):
  - [create](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/ipc/semaphore.cpp#L14-L25)
  - [wait (P)](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/ipc/semaphore.cpp#L28-L43)
  - [post (V)](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/ipc/semaphore.cpp#L45-L60)
  - [destroy](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/ipc/semaphore.cpp#L62-L73)
  - [open](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/ipc/semaphore.cpp#L75-L83)
- Shared memory (`shmget`/`shmat`/`shmdt`/`shmctl`):
  - [create](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/ipc/shared_memory.cpp#L14-L22)
  - [attach](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/ipc/shared_memory.cpp#L25-L37)
  - [detach](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/ipc/shared_memory.cpp#L39-L50)
  - [destroy](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/ipc/shared_memory.cpp#L52-L63)
  - [open](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/ipc/shared_memory.cpp#L65-L72)
- Signals (`sigaction`):
  - [setHandler](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/ipc/signals.cpp#L23-L36)
  - [ignore](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/ipc/signals.cpp#L38-L45)
- Process control: `fork`/`execv`/`kill`/`waitpid` in Director (`sor-simulation/src/director.cpp:486`) and PatientGenerator (`sor-simulation/src/roles/patient_generator.cpp:57`).
- Logging IPC: `runLogger` uses `msgrcv` to consume log messages (`sor-simulation/src/logging/logger.cpp:130`); `logEvent` uses `msgsnd` plus optional `msgctl`/`semctl` metrics (`sor-simulation/src/logging/logger.cpp:183`).

## Role entrypoints (permalinks)
- Director::run: [sor-simulation/src/director.cpp#L490](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/director.cpp#L490)
- runLogger / logEvent: [logger.cpp#L130](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/logging/logger.cpp#L130) / [logger.cpp#L193](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/logging/logger.cpp#L193)
- PatientGenerator::run: [roles/patient_generator.cpp#L57](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/roles/patient_generator.cpp#L57)
- Patient::run: [roles/patient.cpp#L65](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/roles/patient.cpp#L65)
- Registration::run: [roles/registration.cpp#L46](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/roles/registration.cpp#L46)
- Triage::run: [roles/triage.cpp#L77](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/roles/triage.cpp#L77)
- Specialist::run: [roles/specialist.cpp#L75](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/roles/specialist.cpp#L75)

## Debug entrypoints (bypass Director)
```bash
./sor_sim logger <queueId> <logPath>
./sor_sim registration <keyPath>
./sor_sim triage <keyPath>
./sor_sim specialist <keyPath> <typeInt>   # 0..5
./sor_sim patient_generator <keyPath> <N> <K> <simMinutes> <msPerMinute> <seed>
./sor_sim patient <keyPath> <id> <age> <isVip> <hasGuardian> <personsCount>
```

## Assignment highlights (restored)
- Multi-process pipeline using `fork()` + `exec()` for every role (director, logger, registration 1/2, triage, six specialists, patient generator, visualizer).
- System V IPC mix: message queues for registration/triage/specialists/logging; shared memory for global counters; semaphores for waiting-room capacity and shared-state protection.
- Signals: `SIGUSR1` pauses a specialist; `SIGUSR2` triggers evacuation; workers ignore `SIGINT` so shutdown is coordinated by the director.
- Robustness: per-syscall error checks with `errno`, minimal permissions (`0600`), and teardown via `IPC_RMID`/`semctl(IPC_RMID)`/`shmctl(IPC_RMID)` after each run.
- Visualization: dedicated logger produces semicolon-separated lines consumed by the TUI visualizer.

## Compliance notes
- Minimal permissions on IPC objects (`0600`), validation of user input, and cleanup with `IPC_RMID`/`semctl(IPC_RMID)`/`shmctl(IPC_RMID)` are implemented in the referenced snippets.
- Signals in use: `SIGUSR1` (specialist pause), `SIGUSR2` (global stop), `SIGINT` ignored by workers in favor of Director-driven shutdown.
