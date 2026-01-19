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
- Director bootstraps IPC (ftok/msgget/msgctl/semget/shmget) and spawns all children via fork/exec; see [queues](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/director.cpp#L104-L179), [semaphores](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/director.cpp#L181-L214), [shared memory](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/director.cpp#L218-L243), and [process lifecycle](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/director.cpp#L453-L844).
- Logger process blocks on `msgrcv()` until `END`, writing lines with `open`/`write`/`close`: [runLogger](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/logging/logger.cpp#L134-L176).
- PatientGenerator opens IPC and repeatedly `fork()`/`execv()` patients, cleaning up with `kill()`/`waitpid()`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/patient_generator.cpp#L62-L236).
- Patient acquires waiting-room semaphores atomically per household, enqueues via `msgsnd()`, waits for an outcome on the done queue, and honors `SIGUSR2`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/patient.cpp#L70-L278).
- Registration consumes with `msgrcv()`, updates shared state under semaphore, forwards to triage via `msgsnd()`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/registration.cpp#L68-L241).
- Triage reads patients, optionally sends home (posting semaphores) and emits a `PatientDone` outcome, or routes to specialist queues via `msgsnd()`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/triage.cpp#L83-L248).
- Outcome signaling: triage sends `PatientDone` for sent-home cases ([triage.cpp#L182-L196](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/triage.cpp#L182-L196)), specialists send `PatientDone` after handling ([specialist.cpp#L217-L228](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/specialist.cpp#L217-L228)), and patients wait on their `PatientDone` mtype ([patient.cpp#L253-L268](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/patient.cpp#L253-L268)).
- Done queue key: director uses `ftok(..., 'Z')` to keep the done queue distinct from specialist queues (`'A' + i`): [director.cpp#L112-L118](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/director.cpp#L112-L118).
- Specialists handle `SIGUSR1`/`SIGUSR2`, consume prioritized patients with `msgrcv()`, update shared outcomes, and emit `PatientDone`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/specialist.cpp#L84-L238).
- Visualizer tails the log file; when launched by Director it uses the configured refresh interval (no IPC).

## IPC reference (system calls and wrappers)
- Message queues (`msgget`/`msgsnd`/`msgrcv`/`msgctl`):
  - [create](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/message_queue.cpp#L15-L22)
  - [send](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/message_queue.cpp#L25-L45)
  - [receive](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/message_queue.cpp#L48-L64)
  - [destroy](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/message_queue.cpp#L71-L80)
  - [open](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/message_queue.cpp#L84-L90)
- Semaphores (`semget`/`semop`/`semctl`):
  - [create](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/semaphore.cpp#L15-L25)
  - [wait (P)](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/semaphore.cpp#L28-L51)
  - [post (V)](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/semaphore.cpp#L53-L75)
  - [destroy](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/semaphore.cpp#L77-L86)
  - [open](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/semaphore.cpp#L90-L97)
- Shared memory (`shmget`/`shmat`/`shmdt`/`shmctl`):
  - [create](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/shared_memory.cpp#L15-L23)
  - [attach](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/shared_memory.cpp#L26-L37)
  - [detach](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/shared_memory.cpp#L40-L50)
  - [destroy](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/shared_memory.cpp#L53-L63)
  - [open](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/shared_memory.cpp#L66-L73)
- Signals (`sigaction`):
  - [setHandler](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/signals.cpp#L23-L35)
  - [ignore](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/signals.cpp#L38-L45)
- Process control: `fork`/`execv`/`kill`/`waitpid` in Director (`sor-simulation/src/director.cpp:453`) and PatientGenerator (`sor-simulation/src/roles/patient_generator.cpp:62`).
- Logging IPC: `runLogger` uses `msgrcv` to consume log messages (`sor-simulation/src/logging/logger.cpp:134`); `logEvent` uses `msgsnd` plus optional `msgctl`/`semctl` metrics (`sor-simulation/src/logging/logger.cpp:184`).

## Role entrypoints (permalinks)
- Director::run: [sor-simulation/src/director.cpp#L453](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/director.cpp#L453)
- runLogger / logEvent: [logger.cpp#L134](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/logging/logger.cpp#L134) / [logger.cpp#L184](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/logging/logger.cpp#L184)
- PatientGenerator::run: [roles/patient_generator.cpp#L62](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/patient_generator.cpp#L62)
- Patient::run: [roles/patient.cpp#L70](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/patient.cpp#L70)
- Registration::run: [roles/registration.cpp#L68](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/registration.cpp#L68)
- Triage::run: [roles/triage.cpp#L83](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/triage.cpp#L83)
- Specialist::run: [roles/specialist.cpp#L84](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/specialist.cpp#L84)

## Function docs
### sor-simulation/include/director.hpp
- `Director::run(selfPath, config, logPathOverride)` - Entry point for the director process. Params: `selfPath` path to current executable, `config` validated configuration values, `logPathOverride` optional log path (nullptr uses timestamp). Returns 0 on clean shutdown, non-zero on failure.
- `Director::lastSummaryPath()` - Path to the most recently written summary file (text variant); empty if none was produced in the last run.
- `Director::lastLogPath()` - Path to the log file used in the last run.

### sor-simulation/src/director.cpp
- `writeStdout(msg)` - Write a string to stdout with retry on EINTR. Param: `msg` text to write. Returns void.
- `handleSigint(signum)` - SIGINT handler sets stop flags. Param: `signum` signal number. Returns void.
- `handleSigusr2(signum)` - SIGUSR2 handler sets stop flags. Param: `signum` signal number. Returns void.
- `monotonicMs()` - CLOCK_MONOTONIC time in ms (0 on failure). Returns milliseconds.
- `simMinutesFrom(startMs, scaleMsPerMinute)` - Sim minutes since `startMs` given scale. Params: `startMs`, `scaleMsPerMinute`. Returns minutes.
- `realMinutesFrom(startMs)` - Real minutes since `startMs`. Param: `startMs`. Returns minutes.
- `createQueues(keyPath, ids)` - Set up logger/registration/triage/specialist queues, clearing stale ones and tuning capacity via `ftok`, `msgget`, `msgctl`. Params: `keyPath` for ftok keys, `ids` output IPC ids. Returns true on success, false on failure.
- `createSemaphores(keyPath, cfg, ids)` - Create waiting-room and shared-state semaphores via `ftok`, `semget`, `semctl`. Params: `keyPath`, `cfg` for initial values, `ids` output IPC ids. Returns true on success, false on failure.
- `createSharedState(keyPath, ids, stateOut)` - Allocate and attach shared memory for SharedState via `ftok`, `shmget`, `shmat`, `shmctl`. Params: `keyPath`, `ids` output IPC ids, `stateOut` attached pointer. Returns true on success, false on failure.
- `formatDuration(seconds)` - Format seconds as human-readable duration. Param: `seconds` duration in seconds. Returns formatted string.
- `forkExec(exePath, argv, forkErrMsg, execErrMsg)` - Fork and exec a child process with argv, logging fork/exec errors; parent gets child pid, child exits on exec failure.
- `buildPayload(state, simulatedSeconds, realSeconds, reg1PidHistory, reg2PidHistory, triagePidHistory, specialistsPidHistory)` - Build summary payload from shared state and history. Params: `state`, `simulatedSeconds`, `realSeconds`, vectors of pid histories. Returns SummaryPayload.
- `joinHistory(values)` - Join a list of pids into a string. Param: `values` list of pids. Returns joined string.
- `writeSummaryText(payload, out)` - Write text summary to stream. Params: `payload`, `out` output stream. Returns true on success.
- `writeSummary(payload, path)` - Write summary to file path. Params: `payload`, `path`. Returns true on success.
- `destroyIpc(ids, attachedState)` - Detach shared memory and remove IPC objects via `shmdt`, `shmctl(IPC_RMID)`, `msgctl(IPC_RMID)`, `semctl(IPC_RMID)`. Params: `ids` IPC ids, `attachedState` attached pointer (nullable). Returns void.
- `Director::run(selfPath, config, logPathOverride)` - Implementation of director entrypoint. Params/return as in header.
- Local lambdas in this file:
  - `removeIfExists(key)` - Best-effort removal of stale queues/semaphores. Param: `key` System V key.
  - `tuneQueue(qid)` - Increase per-queue capacity. Param: `qid` message queue id.

### sor-simulation/src/main.cpp
- `writeStdout(msg)` - Write a string to stdout with retry on EINTR. Param: `msg` text to write. Returns void.
- `parseConfigFile(path, cfg, err)` - Load key/value pairs from config file with defaults and validation. Params: `path` config path, `cfg` output structure, `err` error message on failure. Returns true if parsed/validated.
- `main(argc, argv)` - Program entrypoint for director. Params: `argc`, `argv`. Returns process exit code.

### sor-simulation/include/roles/patient.hpp
- `Patient::run(keyPath, patientId, age, isVip, hasGuardian, personsCount)` - Execute patient journey (registration -> triage -> specialist). Params: `keyPath` ftok path, `patientId` logical id, `age` in years, `isVip` flag, `hasGuardian` flag, `personsCount` 1 or 2. Returns 0 on completion or orderly shutdown.

### sor-simulation/src/roles/patient.cpp
- `handleSigusr2(signum)` - SIGUSR2 handler sets stop flag. Param: `signum` signal number. Returns void.
- `monotonicMs()` - CLOCK_MONOTONIC time in ms (0 on failure). Returns milliseconds.
- `currentSimMinutes(state)` - Sim minutes derived from shared state timing. Param: `state` shared state ptr. Returns minutes.
- `childThreadMain(arg)` - Guardian helper thread: logs presence and waits for stop flag to end. Param: `arg` pointer to ChildArgs. Returns thread result (nullptr on normal exit).
- `Patient::run(keyPath, patientId, age, isVip, hasGuardian, personsCount)` - Implementation of patient entrypoint via `ftok`, `msgget`, `msgsnd`, `msgrcv`, `semget`, `semop`, `shmget`, `shmat`, `shmdt`. Params/return as in header.
- Local lambdas in this file:
  - `releaseSlotsAndCounters(slots)` - Return waiting-room capacity and roll back shared counters. Param: `slots` number of tokens to release.
  - `stopChildThread()` - Ensure guardian helper thread is stopped and cleaned up.

### sor-simulation/include/roles/registration.hpp
- `Registration::run(keyPath, isSecond)` - Process incoming patients and forward to triage. Params: `keyPath` ftok path, `isSecond` true for second window. Returns 0 on normal exit.

### sor-simulation/src/roles/registration.cpp
- `handleSigusr2(signum)` - SIGUSR2 handler sets stop flags. Param: `signum` signal number. Returns void.
- `monotonicMs()` - Monotonic clock in milliseconds (best effort).
- `currentSimMinutes(state)` - Derive simulation minutes from shared state timing.
- `queueLen(qid)` - Safe queue length probe (0 on error).
- `semaphoreValue(semId)` - Safe semaphore value probe (0 on error).
- `Registration::run(keyPath, isSecond)` - Implementation of registration entrypoint via `ftok`, `msgget`, `msgsnd`, `msgrcv`, `semget`, `semop`, `shmget`, `shmat`, `shmdt`. Params/return as in header.
- Local lambdas in this file:
  - `releaseSlots(count, errTag)` - Release waiting-room capacity and update shared counters. Params: `count` slots to release, `errTag` error tag for logging.

### sor-simulation/include/roles/triage.hpp
- `Triage::run(keyPath)` - Consume from TRIAGE_QUEUE, assign colors, route to specialists/home. Returns 0 on normal exit.

### sor-simulation/src/roles/triage.cpp
- `handleSigusr2(signum)` - SIGUSR2 handler sets stop flags. Param: `signum` signal number. Returns void.
- `pickSpecialist(rng)` - Uniformly pick a specialist type.
- `pickColor(rng)` - Pick triage color with weighted probabilities.
- `colorPriority(c)` - Priority ordering for colors (lower is higher priority).
- `monotonicMs()` - Monotonic clock in milliseconds (best effort).
- `currentSimMinutes(state)` - Simulation minutes derived from shared state start/time scale.
- `Triage::run(keyPath)` - Implementation of triage entrypoint via `ftok`, `msgget`, `msgsnd`, `msgrcv`, `semget`, `semop`, `shmget`, `shmat`, `shmdt`. Params/return as in header.

### sor-simulation/include/roles/specialist.hpp
- `Specialist::run(keyPath, type)` - Process patients from SPECIALISTS_QUEUE; handle SIGUSR1/SIGUSR2. Returns 0 on normal exit.

### sor-simulation/src/roles/specialist.cpp
- `handleSigusr2(signum)` - SIGUSR2 handler sets stop flags. Param: `signum` signal number. Returns void.
- `handleSigusr1(signum)` - SIGUSR1 handler toggles pause flag. Param: `signum` signal number. Returns void.
- `specToString(t)` - Map enum to human-readable specialist name.
- `roleForType(t)` - Role enum corresponding to specialist type (for logging).
- `maxMsgTypeForSpec(t)` - Highest message type a specialist should accept (priority range).
- `monotonicMs()` - Monotonic clock in milliseconds (best effort).
- `currentSimMinutes(state)` - Derive simulation minutes from shared state timing.
- `Specialist::run(keyPath, type)` - Implementation of specialist entrypoint via `ftok`, `msgget`, `msgsnd`, `msgrcv`, `semget`, `semop`, `shmget`, `shmat`, `shmdt`. Params/return as in header.

### sor-simulation/include/roles/patient_generator.hpp
- `PatientGenerator::run(keyPath, cfg)` - Main loop for spawning patients. Params: `keyPath` ftok path, `cfg` configuration (time scale, totals, seed). Returns 0 on normal stop, non-zero on error.

### sor-simulation/src/roles/patient_generator.cpp
- `handleSigusr2(signum)` - SIGUSR2 handler sets stop flags. Param: `signum` signal number. Returns void.
- `monotonicMs()` - Monotonic clock in milliseconds (best effort).
- `currentSimMinutes(state)` - Simulation minutes derived from shared state start/time scale.
- `currentRealMinutes(state)` - Real minutes elapsed since sim start (wall-clock).
- `PatientGenerator::run(keyPath, cfg)` - Implementation of patient generator entrypoint via `ftok`, `msgget`, `semget`, `semop`, `shmget`, `shmat`, `shmdt`. Params/return as in header.
- Local lambdas in this file:
  - `reapChildren(list)` - Reap finished children to avoid zombies and free process slots. Param: `list` vector of child pids.
  - `scaleInterval(baseMs)` - Scale intervals with sim speed; clamp to at least 1 ms for positive inputs. Param: `baseMs` base interval in ms.

### sor-simulation/include/ipc/message_queue.hpp
- `MessageQueue::MessageQueue()` - Construct an empty handle (mqId = -1).
- `MessageQueue::~MessageQueue()` - Destructor (no IPC calls).
- `MessageQueue::create(key, permissions)` - Create or get a queue for the given key via `msgget`. Params: `key` System V key, `permissions` mode (default 0600). Returns true on success.
- `MessageQueue::send(msg, size, type)` - Send a message of a given type via `msgsnd`. Params: `msg` buffer, `size` bytes, `type` mtype. Returns true on success.
- `MessageQueue::receive(buffer, size, type, flags)` - Receive a message of a given type via `msgrcv`. Params: `buffer` destination, `size` bytes, `type` mtype (0 any), `flags` msgrcv flags. Returns true on success.
- `MessageQueue::id()` - Underlying queue id, or -1 if not created.
- `MessageQueue::destroy()` - Remove the queue from the system via `msgctl(IPC_RMID)`. Returns true on success.
- `MessageQueue::open(key)` - Open an existing queue without reinitializing via `msgget`. Returns true on success.

### sor-simulation/include/ipc/semaphore.hpp
- `Semaphore::Semaphore()` - Construct an empty handle (semId = -1).
- `Semaphore::~Semaphore()` - Destructor (no IPC calls).
- `Semaphore::create(key, initialValue, permissions)` - Create a semaphore set with one semaphore and initialize it via `semget`/`semctl`. Params: `key` System V key, `initialValue` starting count, `permissions` mode. Returns true on success.
- `Semaphore::wait()` - P operation (decrement or block until available) via `semop`. Returns true on success.
- `Semaphore::wait(count)` - P operation for multiple tokens (atomic) via `semop`. Param: `count` number of tokens. Returns true on success.
- `Semaphore::post()` - V operation (increment/unlock) via `semop`. Returns true on success.
- `Semaphore::post(count)` - V operation for multiple tokens (atomic) via `semop`. Param: `count` number of tokens. Returns true on success.
- `Semaphore::destroy()` - Remove the semaphore set via `semctl(IPC_RMID)`. Returns true on success.
- `Semaphore::open(key)` - Open an existing semaphore set by key via `semget` without reinitializing. Returns true on success.
- `Semaphore::id()` - Underlying semaphore id, or -1 if not created.

### sor-simulation/include/ipc/shared_memory.hpp
- `SharedMemory::SharedMemory()` - Construct an empty handle (shmId = -1).
- `SharedMemory::~SharedMemory()` - Destructor (no IPC calls).
- `SharedMemory::create(key, size, permissions)` - Create or get a shared memory segment via `shmget`. Params: `key` System V key, `size` bytes, `permissions` mode. Returns true on success.
- `SharedMemory::attach()` - Attach the segment to the process address space via `shmat`. Returns pointer on success, nullptr on failure.
- `SharedMemory::detach(addr)` - Detach a previously attached address via `shmdt`. Params: `addr` pointer returned by attach(). Returns true on success.
- `SharedMemory::destroy()` - Mark the segment for destruction via `shmctl(IPC_RMID)`. Returns true on success.
- `SharedMemory::open(key)` - Open an existing segment by key via `shmget` without creating a new one. Returns true on success.
- `SharedMemory::id()` - Underlying shm id, or -1 if not created.

### sor-simulation/include/ipc/signals.hpp
- `Signals::setHandler(signum, handler)` - Install a handler for a given signal via `sigaction`. Params: `signum` signal number, `handler` function/lambda taking the signal number. Returns true on success.
- `Signals::ignore(signum)` - Ignore a given signal (SIG_IGN) via `sigaction`. Param: `signum` signal number to ignore.

### sor-simulation/include/logging/logger.hpp
- `Logger::Logger()` - Default constructor leaves fd closed.
- `Logger::Logger(path)` - Construct and open a log file immediately. Param: `path` file path to open/create.
- `Logger::openFile(path)` - Open or create the log file. Param: `path` file path. Returns true on success.
- `Logger::logLine(line)` - Write one log line (implementation appends newline). Param: `line` text to write.
- `Logger::closeFile()` - Close the file descriptor if open.
- `runLogger(queueId, path)` - Blocking logger loop: read LogMessage from queue and write to file via `msgrcv`. Params: `queueId` log queue id, `path` log file path. Returns 0 on clean exit, non-zero on error.
- `setLogMetricsContext(context)` - Set the context used by logEvent to append shared-state metrics.
- `logEvent(queueId, role, simTime, text)` - Send a LogMessage through LOG_QUEUE via `msgsnd`. Params: `queueId`, `role` sender, `simTime` simulated minutes, `text` payload. Returns true on success.

### sor-simulation/src/logging/logger.cpp
- `queueLength(qid)` - Safe queue length probe (0 on error).
- `semaphoreValue(semId)` - Safe semaphore value probe (0 on error).
- `collectMetrics()` - Gather current queue/semaphore/shared-state metrics for log enrichment.
- `roleLabel(roleInt)` - Map role enum value to label string. Param: `roleInt` Role as int. Returns label string.
- `runLogger(queueId, path)` - Implementation of logger loop via `msgrcv`. Params/return as in header.
- `setLogMetricsContext(context)` - Store metrics context for logEvent. Params: `context` metrics context. Returns void.
- `logEvent(queueId, role, simTime, text)` - Implementation of logEvent via `msgsnd`. Params/return as in header.

### sor-simulation/include/util/error.hpp
- `die(message)` - Error handling helper for system-call failures.
- `logErrno(message)` - Log a message along with errno details.

### sor-simulation/include/util/random.hpp
- `RandomGenerator::RandomGenerator()` - Seed with std::random_device for non-deterministic runs.
- `RandomGenerator::RandomGenerator(seed)` - Seed with a fixed value for deterministic runs.
- `RandomGenerator::uniformInt(min, max)` - Inclusive integer range [min, max].
- `RandomGenerator::uniformReal(min, max)` - Real range [min, max) using uniform_real_distribution.

### sor-simulation/include/visualization/visualizer.hpp
- `runVisualizer(logPath, renderIntervalMs)` - TUI-like visualizer that tails the simulation log and renders patient flow. Params: `logPath` log file path, `renderIntervalMs` refresh interval (ms). Returns 0 on normal exit, non-zero on error.

### sor-simulation/src/visualization/visualizer.cpp
- `handleSigint(signum)` - SIGINT handler that requests stop. Param: `signum` signal number. Returns void.
- `VisualizerApp::waitForLog()` - Wait for the log file to appear and open it. Returns true on success.
- `VisualizerApp::pumpLines()` - Read new log lines, updating state. Returns true if any new lines processed.
- `VisualizerApp::maybeRender(advanced)` - Render if new data or interval elapsed. Param: `advanced` whether state advanced. Returns void.
- `VisualizerApp::run()` - Visualizer loop. Returns 0 on clean exit, non-zero on error.
- `runVisualizer(logPath, renderIntervalMs)` - Implementation of visualizer entrypoint. Params/return as in header.

### sor-simulation/include/visualization/render_utils.hpp
- `formatPatientLabel(pv, areaStage)` - Format a patient label for a given stage (color/flags).
- `visibleLength(s)` - Terminal-visible length (ignores ANSI escapes).
- `wrapTokens(tokens, width)` - Wrap tokens into lines constrained by width.
- `padded(s, width)` - Pad/truncate a string to width (ANSI-aware).
- `trimQueue(items, limit, keyFn)` - Trim and sort a queue by key, then cap to limit. Params: `items` vector of pointers, `limit` max size, `keyFn` key selector. Returns void.

### sor-simulation/include/visualization/state.hpp
- `ensurePatient(state, patientId)` - Ensure a PatientView exists for id, returning a reference.
- `applyPatientUpdate(entry, state)` - Apply patient-specific updates derived from a log entry.
- `applyLogEntry(entry, state)` - Apply a log entry to mutate the visualization state.
- `collectPatientsByStage(state, stage)` - Collect pointers to patients filtered by stage.

### sor-simulation/src/visualization/state.cpp
- `ensurePatient(state, patientId)` - Implementation of ensurePatient. Params/return as in header.
- `trackRegistrationLifecycle(entry, state)` - Update registration lifecycle counters from a log entry. Params: `entry`, `state`. Returns void.
- `specialistIndexByPid(state, pid)` - Map specialist pid to index. Params: `state`, `pid`. Returns index or -1.
- `applyPatientUpdate(entry, state)` - Implementation of applyPatientUpdate. Params/return as in header.
- `applyLogEntry(entry, state)` - Implementation of applyLogEntry. Params/return as in header.
- `collectPatientsByStage(state, stage)` - Implementation of collectPatientsByStage. Params/return as in header.

### sor-simulation/include/visualization/renderer.hpp
- `renderTopSection(state)` - Render waiting room / triage / entrance overview with live stats.
- `renderActions(state)` - Render the trailing set of recent log actions.
- `renderSpecialists(state)` - Render specialist queues/active patients and per-specialist stats.
- `render(state)` - Full frame render: clear screen then draw all sections.

### sor-simulation/src/visualization/renderer.cpp
- `writeAll(buf)` - Write a full buffer to stdout with retry on EINTR. Param: `buf` output buffer. Returns void.
- `appendLine(out, line)` - Append a line plus newline to a string buffer. Params: `out` buffer, `line` line to append. Returns void.
- `renderTopSection(state)` - Implementation of renderTopSection. Params/return as in header.
- `renderActions(state)` - Implementation of renderActions. Params/return as in header.
- `renderSpecialists(state)` - Implementation of renderSpecialists. Params/return as in header.
- `render(state)` - Implementation of render. Params/return as in header.

### sor-simulation/src/ipc/signals.cpp
- `handlerMap()` - Static map of registered signal handlers. Returns reference.
- `dispatch(signum)` - Dispatch to registered handler for signum. Param: `signum` signal number. Returns void.

## Logs
Logger writes semicolon-separated lines to the log file. Two variants exist depending on whether metrics context is enabled:
- Minimal line: `simTime;pid;text`
- Metrics-enriched line: `simTime;pid;wR=<inside>/<capacity>;rQ=<len>;tQ=<len>;sQ=<len>;wSem=<value>;sSem=<value>;role;text`

Fields and meanings:
- `simTime` simulated minutes at event time.
- `pid` process id of the emitter.
- `wR` waiting room inside/capacity (from shared state).
- `rQ` registration queue length.
- `tQ` triage queue length.
- `sQ` total specialist queue length (sum of specialist queues).
- `wSem` waiting-room semaphore value.
- `sSem` shared-state semaphore value.
- `role` lower-case role label (`director`, `patient`, `registration`, `triage`, `specialist`, `logger`, `unknown`).
- `text` free-form message content (e.g., arrival, routing, outcome).

Special cases:
- Line where `text` starts with `END` stops the logger loop and terminates logging.

Enums used in text payloads:
- `triageColorInt`: `0=Red`, `1=Yellow`, `2=Green`.
- `specIdx`: specialist index in order `0=Cardiologist`, `1=Neurologist`, `2=Ophthalmologist`, `3=Laryngologist`, `4=Surgeon`, `5=Paediatrician`.

Text field formats (by role, without metrics prefix):
- Director:
  - `Director: IPC initialized, logger spawned: <logPath>`
  - `Simulation config N=<int> K=<int> simMinutes=<int> msPerMinute=<int> regMs=<int> triageMs=<int> specMinMax=<min>/<max> leaveMinMax=<min>/<max> reconcileWaitSem=<0|1>`
  - `Director PIDs: reg1=<pid> reg2=<pid> triage=<pid> gen=<pid>`
  - `Registration1 spawned`
  - `Triage spawned`
  - `Patient generator spawned`
  - `Specialist spawned type <idx>`
  - `Registration2 spawned (regQ=<len> waitingRoom=<inside>/<capacity>)`
  - `Registration2 closing (regQ=<len> waitingRoom=<inside>/<capacity>)`
  - `Simulation duration reached (<minutes> min)`
  - `Force killed <name>`
  - `ERROR MON RECONCILE set waitSem from <old> to <expected> missing=<n> pid=<pid> n=<waiters> z=<zeroWaiters> setRes=<res>`
  - `ERROR MON w=<semVal> id=<semId> miss=<missing> pid=<pid> n=<waiters> z=<zeroWaiters> ot=<semOtime> r1=<0|1> r2=<0|1> t=<0|1>`
  - `Director sent SIGUSR1 to specialist pid=<pid>`
  - `Director received SIGUSR2, broadcasting shutdown`
  - `Director received SIGINT (Ctrl+C), broadcasting SIGUSR2`
  - `Director received stop request, broadcasting SIGUSR2`
  - `Director initiating shutdown (SIGUSR2 to children)`
  - `Summary saved: <path>`
  - `END`
- PatientGenerator:
  - `PatientGenerator running (until SIGUSR2)`
  - `PatientGenerator waiting for children slots (count=<n>)`
  - `PatientGenerator fork failed, backing off`
  - `PatientGenerator stopping (SIGUSR2)` or `PatientGenerator stopping`
- Registration:
  - `Registration started` or `Registration2 started`
  - `Registering patient id=<id> vip=<0|1> persons=<n>`
  - `Forwarded patient id=<id> vip=<0|1> persons=<n>`
  - `Dropped patient id=<id> due to triage send failure; released waiting room slots`
  - `HEARTBEAT REG qLen=<len> waitSem=<val> inside=<n> regPid=<pid>`
  - `Registration shutting down (SIGUSR2)` or `Registration2 shutting down (SIGUSR2)`
  - `Registration shutting down` or `Registration2 shutting down`
- Triage:
  - `Triage started`
  - `Patient sent home from triage id=<id>`
  - `Forwarded patient id=<id> to specialist=<idx> color=<triageColorInt>`
  - `Triage shutting down (SIGUSR2)` or `Triage shutting down`
- Specialist:
  - `Specialist <Name> started`
  - `SIGUSR1: temporary leave finished`
  - `Received patient id=<id> color=<triageColorInt> persons=<n>`
  - `Handled patient id=<id> outcome=<home|ward|otherFacility> persons=<n> color=<triageColorInt> specIdx=<idx>`
  - `Specialist shutting down (SIGUSR2)` or `Specialist shutting down`
- Patient:
  - `Child thread active for patient id=<id>`
  - `Child thread exiting for patient id=<id>`
  - `Patient waiting to enter waiting room id=<id> persons=<n>`
  - `ERROR waitSem wait failed id=<id> persons=<n>`
  - `Patient arrived id=<id> age=<years> vip=<0|1> persons=<n> guardian=<0|1>`
  - `Patient registered id=<id>`
  - `Patient done id=<id> outcome=<home|ward|otherFacility>`

Summary file format (`sor_summary_<timestamp>.txt`):
- Header: `SOR Simulation Summary` + underline.
- Counters: total processed, waiting room capacity, registration queue length at shutdown.
- Triage outcomes: red/yellow/green/sent home.
- Final dispositions: home/ward/other.
- Time info: `Time scale (ms per minute)`, `Simulation duration (config minutes)`, `Simulated elapsed time`.
- Process IDs: director, registration1, triage, specialists list, registration2 history.

### sor-simulation/include/visualization/log_parser.hpp
- `toIntSafe(s)` - Safe stoi returning 0 on failure. Param: `s` string. Returns int.
- `extractInt(text, key, out)` - Extract integer value for a given key in free-form text. Params: `text`, `key`, `out` output int. Returns true on success.
- `split(line, delim)` - Split string by delimiter into parts. Params: `line`, `delim`. Returns vector of parts.
- `colorFromInt(value)` - Map integer to TriageColor. Param: `value` int. Returns color enum.
- `specialistFromInt(value)` - Map integer to SpecialistType. Param: `value` int. Returns specialist enum.
- `specialistName(t)` - Short uppercase specialist label. Param: `t` specialist type. Returns string.
- `specialistNameColored(t)` - Short colored specialist label for terminal output. Param: `t` specialist type. Returns string.
- `specialistFromLabel(text)` - Infer SpecialistType from descriptive label text. Param: `text` label. Returns specialist enum.
- `parseLogLine(line, out)` - Parse a log line into structured LogEntry (handles metric-prefixed format). Params: `line`, `out` output entry. Returns true on success.
- `render(state)` - Full frame render: clears screen then draws all sections.

### sor-simulation/include/visualization/log_parser.hpp
- `toIntSafe(s)` - Safe stoi returning 0 on failure.
- `extractInt(text, key, out)` - Extract integer value for a given key in free-form text.
- `split(line, delim)` - Split string by delimiter into parts.
- `colorFromInt(value)` - Map integer to TriageColor.
- `specialistFromInt(value)` - Map integer to SpecialistType.
- `specialistName(t)` - Short uppercase specialist label.
- `specialistNameColored(t)` - Short colored specialist label for terminal output.
- `specialistFromLabel(text)` - Infer SpecialistType from descriptive label text.
- `parseLogLine(line, out)` - Parse a log line into structured LogEntry (handles metric-prefixed format).

## Debug entrypoints (bypass Director)
```bash
./sor_sim logger <queueId> <logPath>
./sor_sim registration <keyPath>
./sor_sim triage <keyPath>
./sor_sim specialist <keyPath> <typeInt>   # 0..5
./sor_sim patient_generator <keyPath> <N> <K> <simMinutes> <msPerMinute> <seed>
./sor_sim patient <keyPath> <id> <age> <isVip> <hasGuardian> <personsCount>
```

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
