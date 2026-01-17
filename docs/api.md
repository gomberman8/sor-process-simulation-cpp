# SOR Simulation – Implementation Guide

Concise reference of the IPC-heavy workflow with links into the code (paths are repo-local and line-precise).

## Environment & limits
- Toolchain: C++17 with CMake (`cmake -S . -B build && cmake --build build`), no external dependencies beyond the standard library and SysV IPC. Tested on Debian x86_64; SysV IPC on macOS is unreliable, so run on Linux for evaluation.
- IPC hygiene: objects created with 0600 permissions and removed on shutdown; `ipcs` should be empty after normal exit. Signals installed via `sigaction` for `SIGUSR1`/`SIGUSR2`/`SIGINT`.

## Runtime workflow (permalinks)
- **Director** – boots IPC (`ftok`/`msgget`/`semget`/`shmget`), spawns children with `fork()`/`execv()`, coordinates shutdown with `kill()`/`waitpid()`, and removes IPC via `IPC_RMID`/`semctl`/`shmctl`. See [queues](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/director.cpp#L104-L170), [semaphores](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/director.cpp#L181-L214), [shared memory](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/director.cpp#L218-L246), and [process lifecycle](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/director.cpp#L424-L844).
- **Logger** – dedicated process blocking on `msgrcv()` until an `END` marker, writing lines to a file opened with `open()/write()/close()`: [runLogger](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/logging/logger.cpp#L133-L176).
- **PatientGenerator** – opens existing IPC via `ftok`/`msgget`/`shmget`/`semget`, then repeatedly `fork()`/`execv()` patients, using `waitpid()`/`kill()` for cleanup: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/patient_generator.cpp#L62-L236).
- **Patient** – attaches to queues/semaphores/shared memory, acquires waiting-room slots atomically with `semop`, enqueues via `msgsnd()`, waits for `PatientDone` on the done queue, and responds to `SIGUSR2`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/patient.cpp#L70-L278).
- **Registration** – pulls from the registration queue with `msgrcv()`, updates shared counters, forwards to triage via `msgsnd()`, exits on `SIGUSR2`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/registration.cpp#L68-L241).
- **Triage** – consumes from triage with `msgrcv()`, posts semaphores for patients sent home, emits `PatientDone`, routes others to specialists with `msgsnd()`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/triage.cpp#L83-L241).
- **Specialist** – handles `SIGUSR1`/`SIGUSR2`, receives prioritized patients with `msgrcv()`, updates outcomes in shared memory, emits `PatientDone`, logs: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/specialist.cpp#L84-L238).
- **Outcome signaling** – triage emits `PatientDone` for send-home cases ([triage.cpp#L182-L196](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/triage.cpp#L182-L196)), specialists emit `PatientDone` after an exam ([specialist.cpp#L217-L228](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/specialist.cpp#L217-L228)), and patients block on their `PatientDone` mtype ([patient.cpp#L253-L268](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/roles/patient.cpp#L253-L268)).
- **Done queue key** – director uses `ftok(..., 'Z')` to keep the done queue distinct from specialist queues (`'A' + i`): [director.cpp#L112-L118](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/director.cpp#L112-L118).

## IPC wrappers (SysV)
- **MessageQueue** (`msgget`/`msgsnd`/`msgrcv`/`msgctl`):
  - [create](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/message_queue.cpp#L14-L22)
  - [send](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/message_queue.cpp#L24-L45)
  - [receive](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/message_queue.cpp#L47-L64)
  - [destroy](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/message_queue.cpp#L70-L80)
  - [open](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/message_queue.cpp#L83-L90)
- **SharedMemory** (`shmget`/`shmat`/`shmdt`/`shmctl`):
  - [create](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/shared_memory.cpp#L14-L23)
  - [attach](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/shared_memory.cpp#L25-L37)
  - [detach](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/shared_memory.cpp#L39-L50)
  - [destroy](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/shared_memory.cpp#L52-L63)
  - [open](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/shared_memory.cpp#L65-L73)
- **Semaphore** (`semget`/`semop`/`semctl`):
  - [create](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/semaphore.cpp#L14-L25)
  - [wait (P)](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/semaphore.cpp#L28-L51)
  - [post (V)](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/semaphore.cpp#L53-L75)
  - [destroy](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/semaphore.cpp#L76-L87)
  - [open](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/semaphore.cpp#L89-L97)
- **Signals** (`sigaction`):
  - [setHandler](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/signals.cpp#L23-L36)
  - [ignore](https://github.com/gomberman8/sor-process-simulation-cpp/blob/main/sor-simulation/src/ipc/signals.cpp#L38-L45)

## Roles (what each does)
- **Director** – owns lifecycle and IPC cleanup (`sor-simulation/src/director.cpp:424`).
- **PatientGenerator** – produces patients at the configured pace (`sor-simulation/src/roles/patient_generator.cpp:62`).
- **Patient** – models entry, waiting-room semaphore usage, queueing, and waits for `PatientDone` (`sor-simulation/src/roles/patient.cpp:70`).
- **Registration** – dequeues arrivals, forwards to triage (`sor-simulation/src/roles/registration.cpp:68`).
- **Triage** – color assignment, optional dismissal, specialist routing, emits `PatientDone` on send-home (`sor-simulation/src/roles/triage.cpp:83`).
- **Specialist** – exam/outcome, responds to director signals, emits `PatientDone` (`sor-simulation/src/roles/specialist.cpp:84`).
- **Logger** – consumes log queue and writes to file (`sor-simulation/src/logging/logger.cpp:133`).

## Role entrypoints (exact lines)
- Director::run: `sor-simulation/src/director.cpp:424`
- runLogger/logEvent: `sor-simulation/src/logging/logger.cpp:133` / `sor-simulation/src/logging/logger.cpp:184`
- PatientGenerator::run: `sor-simulation/src/roles/patient_generator.cpp:62`
- Patient::run: `sor-simulation/src/roles/patient.cpp:70`
- Registration::run: `sor-simulation/src/roles/registration.cpp:68`
- Triage::run: `sor-simulation/src/roles/triage.cpp:83`
- Specialist::run: `sor-simulation/src/roles/specialist.cpp:84`

## Data structures
- **Events & roles**: enums and message payloads in `sor-simulation/include/model/events.hpp` and `sor-simulation/include/model/types.hpp` (includes `EventType::PatientDone` for outcome signaling).
- **Shared state**: counts, queue lengths, and PIDs in `sor-simulation/include/model/shared_state.hpp`.
- **Config**: runtime knobs in `sor-simulation/include/model/config.hpp` and `sor-simulation/config.cfg`.

All IPC operations use minimal permissions (0600) and validate return codes with `errno` logging. Cleanup paths remove queues/semaphores/shared memory after the run.
