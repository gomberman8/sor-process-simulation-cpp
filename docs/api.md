# SOR Simulation – API & Expected Behaviors

This document describes the intended behavior, inputs, outputs, and example usage for all custom classes/functions currently present in the skeleton. Implementations are stubs right now; use these contracts when filling in real logic.

## IPC Wrappers

### `MessageQueue` (`ipc/message_queue.hpp/.cpp`)
- **Purpose:** Thin RAII-ish wrapper over System V message queues.
- **Methods:**
  - `bool create(key_t key, int permissions = 0600);`
    - Creates/opens a queue with minimal required permissions.
    - **Returns:** `true` on success; `false` on failure (log `errno`).
    - **Example:**  
      ```cpp
      MessageQueue mq;
      if (!mq.create(IPC_PRIVATE, 0600)) { /* handle error */ }
      ```
  - `bool send(const void* msg, size_t size, long type);`
    - Wraps `msgsnd`; sends `size` bytes where `((long*)msg)[0]` equals `type`.
    - **Returns:** `true` on success; `false` on failure.
    - **Example:**  
      ```cpp
      EventMessage ev{}; ev.mtype = static_cast<long>(EventType::PatientArrived);
      mq.send(&ev, sizeof(ev), ev.mtype);
      ```
  - `bool receive(void* buffer, size_t size, long type, int flags = 0);`
    - Wraps `msgrcv`; reads message of `type` (or first if `type == 0`).
    - **Returns:** `true` on success; `false` on failure.
    - **Example:**  
      ```cpp
      EventMessage ev{};
      mq.receive(&ev, sizeof(ev), static_cast<long>(EventType::PatientArrived));
      ```
  - `int id() const;` → the underlying queue id (`-1` if not created).

### `SharedMemory` (`ipc/shared_memory.hpp/.cpp`)
- **Purpose:** Manage System V shared memory segment for `SharedState`.
- **Methods:**
  - `bool create(key_t key, size_t size, int permissions = 0600);`
    - Calls `shmget`; stores `size` for validation.
    - **Returns:** `true` on success; `false` otherwise.
  - `void* attach();`
    - Calls `shmat`; **Returns:** mapped address on success; `nullptr` on failure.
  - `bool detach(const void* addr);`
    - Calls `shmdt`; **Returns:** `true` on success.
  - `bool destroy();`
    - Calls `shmctl(..., IPC_RMID, ...)`; **Returns:** `true` on success.
  - `int id() const;` → underlying shm id (`-1` if not created).
- **Example:**  
  ```cpp
  SharedMemory shm;
  shm.create(IPC_PRIVATE, sizeof(SharedState));
  auto* state = static_cast<SharedState*>(shm.attach());
  // use state...
  shm.detach(state);
  shm.destroy();
  ```

### `Semaphore` (`ipc/semaphore.hpp/.cpp`)
- **Purpose:** System V counting/binary semaphore wrapper.
- **Methods:**
  - `bool create(key_t key, int initialValue, int permissions = 0600);`
    - Uses `semget` + `semctl` to init value.
    - **Returns:** `true` on success.
  - `bool wait();`
    - P operation (`semop` with `-1`); **Returns:** `true` on success.
  - `bool post();`
    - V operation (`semop` with `+1`); **Returns:** `true` on success.
  - `bool destroy();`
    - `semctl(IPC_RMID)`; **Returns:** `true` on success.
  - `int id() const;` → semaphore set id (`-1` if not created).
- **Example:**  
  ```cpp
  Semaphore sem;
  sem.create(IPC_PRIVATE, 1);
  sem.wait();
  // critical section
  sem.post();
  sem.destroy();
  ```

### `Signals` (`ipc/signals.hpp/.cpp`)
- **Purpose:** Helper for installing simple C++ lambda/`std::function` handlers.
- **Functions:**
  - `bool setHandler(int signum, Handler handler);`
    - Registers a handler invoked with the signal number.
    - **Returns:** `true` on success.
  - `void ignore(int signum);`
    - Installs `SIG_IGN` for given signal.
- **Example:**  
  ```cpp
  Signals::setHandler(SIGUSR2, [](int){ /* cleanup and exit */ });
  Signals::ignore(SIGPIPE);
  ```

## Logging

### `Logger` (`logging/logger.hpp/.cpp`)
- **Purpose:** Dedicated process writing lines to a log file.
- **Methods:**
  - `Logger();` / `explicit Logger(const std::string& path);`
    - Default ctor leaves `fd = -1`; path ctor calls `openFile(path)`.
  - `bool openFile(const std::string& path);`
    - Uses `open`/`creat` with minimal permissions.
    - **Returns:** `true` on success; `false` otherwise.
  - `void logLine(const std::string& line);`
    - Writes a single line (plus newline) if `fd` valid.
  - `void closeFile();`
    - Closes descriptor if open.
- **Example:**  
  ```cpp
  Logger logger("sor.log");
  logger.logLine("[SIM=0001][PID=1234][ROLE=TRIAGE] patient triaged");
  logger.closeFile();
  ```

## Utility

### `RandomGenerator` (`util/random.hpp/.cpp`)
- **Purpose:** Deterministic or random PRNG for simulation timings.
- **Methods:**
  - `RandomGenerator();` seeds with `std::random_device`.
  - `explicit RandomGenerator(unsigned int seed);` deterministic seed.
  - `int uniformInt(int min, int max);` inclusive range.
  - `double uniformReal(double min, double max);` inclusive of min, exclusive of max.
- **Example:**  
  ```cpp
  RandomGenerator rng(42);
  int triage = rng.uniformInt(0, 99);   // 0..99
  double waitMs = rng.uniformReal(5.0, 10.0);
  ```

### Error Helpers (`util/error.hpp/.cpp`)
- **Purpose:** Centralized error reporting for system calls.
- **Functions:**
  - `void die(const std::string& message);`
    - Prints message to `stderr`, exits non-zero.
  - `void logErrno(const std::string& message);`
    - Prints message plus `errno` details (to be implemented).
- **Example:**  
  ```cpp
  if (someSyscall() == -1) logErrno("someSyscall failed");
  ```

## Roles / Processes

### `Director` (`director.hpp/.cpp`)
- **Purpose:** Main orchestrator; sets up IPC, spawns all child roles, handles signals/cleanup.
- **Method:**
  - `int run();`
    - Expected to return `0` on clean shutdown; non-zero on failure.
- **Example:**  
  ```cpp
  int main() { Director d; return d.run(); }
  ```

### `PatientGenerator` (`roles/patient_generator.hpp/.cpp`)
- **Purpose:** Periodically spawns `Patient` processes with randomized attributes.
- **Method:**
  - `int run();` → 0 on normal stop.

### `Patient` (`roles/patient.hpp/.cpp`)
- **Purpose:** Models a single (possibly child+guardian) patient moving through SOR pipeline.
- **Method:**
  - `int run();` → 0 on completion or orderly SIGUSR2 shutdown.

### `Registration` (`roles/registration.hpp/.cpp`)
- **Purpose:** Represents one registration window consuming from `REGISTRATION_QUEUE`.
- **Method:**
  - `int run();` → 0 on normal exit.

### `Triage` (`roles/triage.hpp/.cpp`)
- **Purpose:** Assigns triage color, optionally sends home, forwards to specialists.
- **Method:**
  - `int run();` → 0 on normal exit.

### `Specialist` (`roles/specialist.hpp/.cpp`)
- **Purpose:** Handles one specialty; reacts to `SIGUSR1` (temporary leave) and `SIGUSR2` (shutdown).
- **Method:**
  - `int run();` → 0 on normal exit.

**Role Example Launch (conceptual):**
```cpp
pid_t pid = fork();
if (pid == 0) {
    Specialist s;
    return s.run();
} else if (pid > 0) {
    // parent continues
}
```

## Models

- `model/types.hpp`: enums for triage colors, specialist types, event types, and roles.
- `model/patient.hpp`: `PatientInfo` structure describing patient attributes.
- `model/events.hpp`: `EventMessage` for IPC; `LogMessage` for logger queue.
- `model/shared_state.hpp`: `SharedState` shared-memory layout (queue lengths, counts, PIDs).
- `model/config.hpp`: runtime configuration (limits, time scaling, seed, etc.).

These model structs are plain data; no functions. Usage is via the IPC wrappers and role logic described above.
