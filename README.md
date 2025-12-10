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
- Director bootstraps IPC (ftok/msgget/msgctl/semget/shmget) and spawns all children via fork/exec; see [queues](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/director.cpp#L86-L155), [semaphores](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/director.cpp#L158-L192), [shared memory](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/director.cpp#L194-L220), and [process lifecycle](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/director.cpp#L424-L844).
- Logger process blocks on `msgrcv()` until `END`, writing lines with `open`/`write`/`close`: [runLogger](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/logging/logger.cpp#L133-L176).
- PatientGenerator opens IPC and repeatedly `fork()`/`execv()` patients, cleaning up with `kill()`/`waitpid()`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/roles/patient_generator.cpp#L62-L236).
- Patient acquires waiting-room semaphores, enqueues via `msgsnd()`, and honors `SIGUSR2`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/roles/patient.cpp#L70-L274).
- Registration consumes with `msgrcv()`, updates shared state under semaphore, forwards to triage via `msgsnd()`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/roles/registration.cpp#L68-L240).
- Triage reads patients, optionally sends home (posting semaphores), or routes to specialist queues via `msgsnd()`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/roles/triage.cpp#L83-L240).
- Specialists handle `SIGUSR1`/`SIGUSR2`, consume prioritized patients with `msgrcv()`, update shared outcomes: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/roles/specialist.cpp#L84-L232).
- Visualizer tails the log file; when launched by Director it uses the configured refresh interval (no IPC).

## IPC reference (system calls and wrappers)
- Message queues (`msgget`/`msgsnd`/`msgrcv`/`msgctl`):
  - [create](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/ipc/message_queue.cpp#L14-L22)
  - [send](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/ipc/message_queue.cpp#L24-L45)
  - [receive](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/ipc/message_queue.cpp#L47-L64)
  - [destroy](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/ipc/message_queue.cpp#L70-L80)
  - [open](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/ipc/message_queue.cpp#L84-L90)
- Semaphores (`semget`/`semop`/`semctl`):
  - [create](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/ipc/semaphore.cpp#L14-L25)
  - [wait (P)](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/ipc/semaphore.cpp#L28-L44)
  - [post (V)](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/ipc/semaphore.cpp#L47-L61)
  - [destroy](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/ipc/semaphore.cpp#L63-L74)
  - [open](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/ipc/semaphore.cpp#L77-L84)
- Shared memory (`shmget`/`shmat`/`shmdt`/`shmctl`):
  - [create](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/ipc/shared_memory.cpp#L15-L23)
  - [attach](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/ipc/shared_memory.cpp#L26-L37)
  - [detach](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/ipc/shared_memory.cpp#L40-L50)
  - [destroy](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/ipc/shared_memory.cpp#L53-L63)
  - [open](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/ipc/shared_memory.cpp#L66-L73)
- Signals (`sigaction`):
  - [setHandler](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/ipc/signals.cpp#L23-L36)
  - [ignore](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/ipc/signals.cpp#L38-L45)
- Process control: `fork`/`execv`/`kill`/`waitpid` in Director (`sor-simulation/src/director.cpp:424`) and PatientGenerator (`sor-simulation/src/roles/patient_generator.cpp:62`).
- Logging IPC: `runLogger` uses `msgrcv` to consume log messages (`sor-simulation/src/logging/logger.cpp:133`); `logEvent` uses `msgsnd` plus optional `msgctl`/`semctl` metrics (`sor-simulation/src/logging/logger.cpp:183`).

## Role entrypoints (permalinks)
- Director::run: [sor-simulation/src/director.cpp#L424](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/director.cpp#L424)
- runLogger / logEvent: [logger.cpp#L133](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/logging/logger.cpp#L133) / [logger.cpp#L184](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/logging/logger.cpp#L184)
- PatientGenerator::run: [roles/patient_generator.cpp#L62](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/roles/patient_generator.cpp#L62)
- Patient::run: [roles/patient.cpp#L70](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/roles/patient.cpp#L70)
- Registration::run: [roles/registration.cpp#L68](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/roles/registration.cpp#L68)
- Triage::run: [roles/triage.cpp#L83](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/roles/triage.cpp#L83)
- Specialist::run: [roles/specialist.cpp#L84](https://github.com/gomberman8/sor-process-simulation-cpp/blob/c87523231842b27ed441ae7ef8fcabd34eed123e/sor-simulation/src/roles/specialist.cpp#L84)

## Debug entrypoints (bypass Director)
```bash
./sor_sim logger <queueId> <logPath>
./sor_sim registration <keyPath>
./sor_sim triage <keyPath>
./sor_sim specialist <keyPath> <typeInt>   # 0..5
./sor_sim patient_generator <keyPath> <N> <K> <simMinutes> <msPerMinute> <seed>
./sor_sim patient <keyPath> <id> <age> <isVip> <hasGuardian> <personsCount>
```

## Optional reconcile for waiting-room semaphore
- Env flag: `SORSIM_RECONCILE_WAITSEM=1 ./sor_sim --config ../config.cfg`
- Config flag: set `reconcileWaitSem=1` in `config.cfg` (env still overrides).
- What it does: Director monitors the System V semaphore that guards the waiting-room capacity. In long runs I observed rare drift where the kernel semaphore value fell to 0 while the shared counters (acquired/released/inside) were still balanced. With the flag on, whenever Director sees “missing” tokens (expected free slots > current sem value), it resets the semaphore to the expected free count and logs `ERROR MON RECONCILE` with semctl diagnostics. With the flag off (default), nothing is auto-corrected and the raw semaphore value is used.
- Why this exists: System V semaphores do not pair waits/posts across processes and do not auto-return tokens if a process dies after a successful wait. Despite per-call error checks, I saw occasional kernel-side value loss under heavy contention (hundreds of blocked waiters). The reconcile is a guardrail to keep the simulation responsive for demos while still using the required SysV primitives. It is off by default to preserve the original assignment semantics; enabling it is a conscious opt-in when investigating or demonstrating long runs.

## Compliance notes
- Minimal permissions on IPC objects (`0600`), validation of user input, and cleanup with `IPC_RMID`/`semctl(IPC_RMID)`/`shmctl(IPC_RMID)` are implemented in the referenced snippets.
- Signals in use: `SIGUSR1` (specialist pause), `SIGUSR2` (global stop), `SIGINT` ignored by workers in favor of Director-driven shutdown.
