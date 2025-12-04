# SOR Simulation – Implementation Guide

Concise reference of the IPC-heavy workflow with links into the code (paths are repo-local and line-precise).

## Runtime workflow (permalinks)
- **Director** – boots IPC (`ftok`/`msgget`/`semget`/`shmget`), spawns children with `fork()`/`execv()`, coordinates shutdown with `kill()`/`waitpid()`, and removes IPC via `IPC_RMID`/`semctl`/`shmctl`. See [queues](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/director.cpp#L85-L156), [semaphores](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/director.cpp#L158-L193), [shared memory](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/director.cpp#L195-L221), and [process lifecycle](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/director.cpp#L486-L735).
- **Logger** – dedicated process blocking on `msgrcv()` until an `END` marker, writing lines to a file opened with `open()/write()/close()`: [runLogger](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/logging/logger.cpp#L130-L178).
- **PatientGenerator** – opens existing IPC via `ftok`/`msgget`/`shmget`/`semget`, then repeatedly `fork()`/`execv()` patients, using `waitpid()`/`kill()` for cleanup: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/roles/patient_generator.cpp#L57-L220).
- **Patient** – attaches to queues/semaphores/shared memory, acquires waiting-room slots with `semop`, enqueues via `msgsnd()`, and responds to `SIGUSR2`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/roles/patient.cpp#L65-L199).
- **Registration** – pulls from the registration queue with `msgrcv()`, updates shared counters, forwards to triage via `msgsnd()`, exits on `SIGUSR2`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/roles/registration.cpp#L46-L160).
- **Triage** – consumes from triage with `msgrcv()`, posts semaphores for patients sent home, routes others to specialists with `msgsnd()`: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/roles/triage.cpp#L77-L237).
- **Specialist** – handles `SIGUSR1`/`SIGUSR2`, receives prioritized patients with `msgrcv()`, updates outcomes in shared memory, logs: [run](https://github.com/gomberman8/sor-process-simulation-cpp/blob/f0c512978a7176c23f62294852002bc0c4ef3e87/sor-simulation/src/roles/specialist.cpp#L75-L211).

## IPC wrappers (SysV)
- **MessageQueue** (`msgget`/`msgsnd`/`msgrcv`/`msgctl`):
  - create: `sor-simulation/src/ipc/message_queue.cpp:14`
  - send: `sor-simulation/src/ipc/message_queue.cpp:24`
  - receive: `sor-simulation/src/ipc/message_queue.cpp:47`
  - destroy: `sor-simulation/src/ipc/message_queue.cpp:70`
  - open: `sor-simulation/src/ipc/message_queue.cpp:83`
- **SharedMemory** (`shmget`/`shmat`/`shmdt`/`shmctl`):
  - create: `sor-simulation/src/ipc/shared_memory.cpp:14`
  - attach: `sor-simulation/src/ipc/shared_memory.cpp:25`
  - detach: `sor-simulation/src/ipc/shared_memory.cpp:39`
  - destroy: `sor-simulation/src/ipc/shared_memory.cpp:52`
  - open: `sor-simulation/src/ipc/shared_memory.cpp:65`
- **Semaphore** (`semget`/`semop`/`semctl`):
  - create: `sor-simulation/src/ipc/semaphore.cpp:14`
  - wait (P): `sor-simulation/src/ipc/semaphore.cpp:28`
  - post (V): `sor-simulation/src/ipc/semaphore.cpp:45`
  - destroy: `sor-simulation/src/ipc/semaphore.cpp:62`
  - open: `sor-simulation/src/ipc/semaphore.cpp:75`
- **Signals** (`sigaction`):
  - setHandler: `sor-simulation/src/ipc/signals.cpp:23`
  - ignore: `sor-simulation/src/ipc/signals.cpp:38`

## Roles (what each does)
- **Director** – owns lifecycle and IPC cleanup (`sor-simulation/src/director.cpp:486`).
- **PatientGenerator** – produces patients at the configured pace (`sor-simulation/src/roles/patient_generator.cpp:57`).
- **Patient** – models entry, waiting-room semaphore usage, and queueing (`sor-simulation/src/roles/patient.cpp:65`).
- **Registration** – dequeues arrivals, forwards to triage (`sor-simulation/src/roles/registration.cpp:46`).
- **Triage** – color assignment, optional dismissal, specialist routing (`sor-simulation/src/roles/triage.cpp:77`).
- **Specialist** – exam/outcome, responds to director signals (`sor-simulation/src/roles/specialist.cpp:75`).
- **Logger** – consumes log queue and writes to file (`sor-simulation/src/logging/logger.cpp:130`).

## Role entrypoints (exact lines)
- Director::run: `sor-simulation/src/director.cpp:490`
- runLogger/logEvent: `sor-simulation/src/logging/logger.cpp:130` / `sor-simulation/src/logging/logger.cpp:193`
- PatientGenerator::run: `sor-simulation/src/roles/patient_generator.cpp:57`
- Patient::run: `sor-simulation/src/roles/patient.cpp:65`
- Registration::run: `sor-simulation/src/roles/registration.cpp:46`
- Triage::run: `sor-simulation/src/roles/triage.cpp:77`
- Specialist::run: `sor-simulation/src/roles/specialist.cpp:75`

## Data structures
- **Events & roles**: enums and message payloads in `sor-simulation/include/model/events.hpp` and `sor-simulation/include/model/types.hpp`.
- **Shared state**: counts, queue lengths, and PIDs in `sor-simulation/include/model/shared_state.hpp`.
- **Config**: runtime knobs in `sor-simulation/include/model/config.hpp` and `sor-simulation/config.cfg`.

All IPC operations use minimal permissions (0600) and validate return codes with `errno` logging. Cleanup paths remove queues/semaphores/shared memory after the run.
