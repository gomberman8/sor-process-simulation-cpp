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
- Visibility: dedicated logger writes semicolon-separated lines consumed by the TUI visualizer; rendering buffers a full frame and writes via `write(1, ...)` to avoid iostream overhead.

## Environment & limits
- Toolchain: C++17 with CMake (`cmake -S . -B build && cmake --build build`), no external deps beyond libc/SysV IPC. Tested on Debian x86_64; SysV IPC may be unreliable on macOS, so run on Linux for grading.
- Runtime footprint: minimal file descriptors (log file + IPC handles). IPC objects created with 0600 perms and removed at shutdown; `ipcs` should be empty after a clean exit. Signals installed via `sigaction` for `SIGUSR1`/`SIGUSR2`/`SIGINT`.

## End-to-end workflow (with permalinks)
- Director bootstraps IPC (ftok/msgget/msgctl/semget/shmget) and spawns all children via fork/exec; see [queues](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/director.cpp#L86-L155), [semaphores](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/director.cpp#L158-L192), [shared memory](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/director.cpp#L194-L220), and [process lifecycle](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/director.cpp#L424-L844).
- Logger process blocks on `msgrcv()` until `END`, writing lines with `open`/`write`/`close`: [runLogger](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/logging/logger.cpp#L133-L176).
- PatientGenerator opens IPC and repeatedly `fork()`/`execv()` patients, cleaning up with `kill()`/`waitpid()`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/patient_generator.cpp#L62-L236).
- Patient acquires waiting-room semaphores atomically per household, enqueues via `msgsnd()`, and honors `SIGUSR2`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/patient.cpp#L70-L255).
- Registration consumes with `msgrcv()`, updates shared state under semaphore, forwards to triage via `msgsnd()`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/registration.cpp#L68-L241).
- Triage reads patients, optionally sends home (posting semaphores), or routes to specialist queues via `msgsnd()`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/triage.cpp#L83-L241).
- Specialists handle `SIGUSR1`/`SIGUSR2`, consume prioritized patients with `msgrcv()`, update shared outcomes: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/specialist.cpp#L84-L232).
- Visualizer tails the log file; when launched by Director it uses the configured refresh interval (no IPC).

## IPC reference (system calls and wrappers)
- Message queues (`msgget`/`msgsnd`/`msgrcv`/`msgctl`):
  - [create](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/message_queue.cpp#L14-L22)
  - [send](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/message_queue.cpp#L24-L45)
  - [receive](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/message_queue.cpp#L47-L64)
  - [destroy](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/message_queue.cpp#L70-L80)
  - [open](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/message_queue.cpp#L83-L90)
- Semaphores (`semget`/`semop`/`semctl`):
  - [create](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/semaphore.cpp#L14-L25)
  - [wait (P)](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/semaphore.cpp#L28-L51)
  - [post (V)](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/semaphore.cpp#L53-L75)
  - [destroy](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/semaphore.cpp#L76-L87)
  - [open](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/semaphore.cpp#L89-L97)
- Shared memory (`shmget`/`shmat`/`shmdt`/`shmctl`):
  - [create](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/shared_memory.cpp#L14-L23)
  - [attach](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/shared_memory.cpp#L25-L37)
  - [detach](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/shared_memory.cpp#L39-L50)
  - [destroy](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/shared_memory.cpp#L52-L63)
  - [open](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/shared_memory.cpp#L65-L73)
- Signals (`sigaction`):
  - [setHandler](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/signals.cpp#L23-L36)
  - [ignore](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/signals.cpp#L38-L45)
- Process control: `fork`/`execv`/`kill`/`waitpid` in Director (`sor-simulation/src/director.cpp:424`) and PatientGenerator (`sor-simulation/src/roles/patient_generator.cpp:62`).
- Logging IPC: `runLogger` uses `msgrcv` to consume log messages (`sor-simulation/src/logging/logger.cpp:133`); `logEvent` uses `msgsnd` plus optional `msgctl`/`semctl` metrics (`sor-simulation/src/logging/logger.cpp:184`).

## Role entrypoints (permalinks)
- Director::run: [sor-simulation/src/director.cpp#L424](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/director.cpp#L424)
- runLogger / logEvent: [logger.cpp#L133](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/logging/logger.cpp#L133) / [logger.cpp#L184](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/logging/logger.cpp#L184)
- PatientGenerator::run: [roles/patient_generator.cpp#L62](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/patient_generator.cpp#L62)
- Patient::run: [roles/patient.cpp#L70](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/patient.cpp#L70)
- Registration::run: [roles/registration.cpp#L68](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/registration.cpp#L68)
- Triage::run: [roles/triage.cpp#L83](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/triage.cpp#L83)
- Specialist::run: [roles/specialist.cpp#L84](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/specialist.cpp#L84)

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
- Why this exists: System V semaphores do not pair waits/posts across processes and do not auto-return tokens if a process dies after a successful wait. Despite per-call error checks, I saw occasional kernel-side value loss under heavy contention (hundreds of blocked waiters). The reconcile is a guardrail to keep the simulation responsive for demos while still using the required SysV primitives. It is off by default to preserve the original assignment semantics; enabling it is a conscious opt-in when investigating or demonstrating long runs. Waiting-room waits/posts now support atomic multi-token operations to avoid partial grabs when a guardian accompanies a child; reconciliation remains a crash-safety net.

## Tests (ctest)
- Harness: `ctest -V` builds/runs small binaries that launch `sor_sim`, let it run briefly, then signal `SIGUSR2` via `runSimulation` ([sim_runner.cpp](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/tests/sim_runner.cpp#L23-L41)) using a temporary log path helper ([sim_runner.cpp](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/tests/sim_runner.cpp#L10-L21)).
- Helpers: assertions write to stderr and exit non-zero on failure ([asserts.hpp](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/tests/asserts.hpp#L1-L22)); log utilities parse simulator logs and summaries for metrics ([log_utils.hpp](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/tests/log_utils.hpp#L1-L30), [log_utils.cpp](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/tests/log_utils.cpp#L6-L61)).
- `test_waiting_room`: fills waiting room and asserts occupancy never exceeds capacity, proving semaphore integrity ([test_waiting_room.cpp](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/tests/test_waiting_room.cpp#L11-L70)).
- `test_reg2_spawn`: slows registration to force queue growth, verifying Registration2 spawn/close messages ([test_reg2_spawn.cpp](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/tests/test_reg2_spawn.cpp#L10-L60)).
- `test_sigusr2_shutdown`: lets runner send `SIGUSR2` and checks each role logs shutdown ([test_sigusr2_shutdown.cpp](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/tests/test_sigusr2_shutdown.cpp#L11-L59)).
- `test_triage_distribution`: checks triage color proportions stay within broad expected bands ([test_triage_distribution.cpp](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/tests/test_triage_distribution.cpp#L10-L59)).
- `test_outcome_distribution`: ensures specialist outcomes fall within expected percentages and match totals ([test_outcome_distribution.cpp](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/tests/test_outcome_distribution.cpp#L10-L59)).
- `test_vip_guardian`: validates VIP and child-with-guardian paths appear through triage/specialists ([test_vip_guardian.cpp](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/tests/test_vip_guardian.cpp#L10-L62)).
- `test_summary_invariants`: asserts summary totals align with triage/outcome counts ([test_summary_invariants.cpp](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/tests/test_summary_invariants.cpp#L10-L49)).

## Compliance notes
- Minimal permissions on IPC objects (`0600`), validation of user input, and cleanup with `IPC_RMID`/`semctl(IPC_RMID)`/`shmctl(IPC_RMID)` are implemented in the referenced snippets.
- Signals in use: `SIGUSR1` (specialist pause), `SIGUSR2` (global stop), `SIGINT` ignored by workers in favor of Director-driven shutdown.
